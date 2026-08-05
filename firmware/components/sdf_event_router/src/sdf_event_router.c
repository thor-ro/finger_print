#include "sdf_event_router.h"
#include "sdf_config.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#define SDF_EVENT_ROUTER_TASK_NAME "sdf_evt_router"
#define SDF_EVENT_ROUTER_TASK_STACK 3072
#define SDF_EVENT_ROUTER_TASK_PRIORITY 5
#define SDF_EVENT_ROUTER_LOCK_WAIT_MS 100u
/* Upper bound on subscribers dispatched for a single event. This is a
 * fan-out cap for the dispatch snapshot below, not a global subscriber
 * limit - comfortably above the handful of tasks that subscribe to any one
 * event type today. */
#define SDF_EVENT_ROUTER_MAX_DISPATCH 16u

static const char *TAG = "sdf_event_router";

struct sdf_event_router_subscriber {
    sdf_event_router_type_t type;
    sdf_event_router_priority_t min_prio;
    sdf_event_router_cb cb;
    void *ctx;
    struct sdf_event_router_subscriber *next;
};

struct sdf_event_router_state {
    QueueHandle_t queue;
    TaskHandle_t task;
    bool initialized;
    sdf_event_router_subscriber_t *subscribers_by_type[SDF_EVENT_ROUTER_TYPE_COUNT];
    SemaphoreHandle_t lock;
} s_state;

static void sdf_event_router_dispatch_sync(const sdf_event_router_event_t *event)
{
    if (event->type >= SDF_EVENT_ROUTER_TYPE_COUNT) {
        ESP_LOGE(TAG, "Dropping event with invalid type %d", (int)event->type);
        return;
    }

    /* Snapshot matching (cb, ctx) pairs by value while holding the lock,
     * then invoke them after releasing it. sdf_event_router_unsubscribe()
     * frees subscriber nodes under this same lock, so walking the linked
     * list itself outside the lock (the previous implementation) risked a
     * use-after-free if a subscriber unsubscribed mid-dispatch. Copying the
     * plain cb/ctx values means the callbacks stay safe to invoke even if
     * the node they came from is freed the instant we unlock. */
    sdf_event_router_cb cbs[SDF_EVENT_ROUTER_MAX_DISPATCH];
    void *ctxs[SDF_EVENT_ROUTER_MAX_DISPATCH];
    size_t n = 0;

    if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(SDF_EVENT_ROUTER_LOCK_WAIT_MS)) != pdTRUE) {
        ESP_LOGW(TAG, "Dispatch lock timeout, dropping event type=%d", (int)event->type);
        return;
    }

    sdf_event_router_subscriber_t *sub = s_state.subscribers_by_type[event->type];
    while (sub != NULL) {
        if (sub->min_prio >= event->priority && sub->cb != NULL) {
            if (n < SDF_EVENT_ROUTER_MAX_DISPATCH) {
                cbs[n] = sub->cb;
                ctxs[n] = sub->ctx;
                n++;
            } else {
                ESP_LOGW(TAG, "Dispatch fan-out cap reached for type=%d", (int)event->type);
                break;
            }
        }
        sub = sub->next;
    }

    xSemaphoreGive(s_state.lock);

    for (size_t i = 0; i < n; i++) {
        cbs[i](ctxs[i], event);
    }
}

static void sdf_event_router_task(void *arg)
{
    sdf_event_router_event_t event;

    while (true) {
        if (xQueueReceive(s_state.queue, &event, portMAX_DELAY) == pdTRUE) {
            sdf_event_router_dispatch_sync(&event);
        }
    }
}

esp_err_t sdf_event_router_init(void)
{
    if (s_state.initialized) {
        return ESP_OK;
    }

    memset(&s_state, 0, sizeof(s_state));

    s_state.lock = xSemaphoreCreateMutex();
    if (s_state.lock == NULL) {
        return ESP_ERR_NO_MEM;
    }

    uint32_t queue_depth = sdf_config_get()->event_router_queue_depth;
    s_state.queue = xQueueCreate(queue_depth, sizeof(sdf_event_router_event_t));
    if (s_state.queue == NULL) {
        vSemaphoreDelete(s_state.lock);
        return ESP_ERR_NO_MEM;
    }

    BaseType_t task_ok = xTaskCreate(sdf_event_router_task,
                                    SDF_EVENT_ROUTER_TASK_NAME,
                                    SDF_EVENT_ROUTER_TASK_STACK,
                                    NULL,
                                    SDF_EVENT_ROUTER_TASK_PRIORITY,
                                    &s_state.task);
    if (task_ok != pdPASS) {
        vQueueDelete(s_state.queue);
        vSemaphoreDelete(s_state.lock);
        return ESP_FAIL;
    }

    s_state.initialized = true;
    ESP_LOGI(TAG, "Event router initialized (queue depth=%d)", queue_depth);
    return ESP_OK;
}

esp_err_t sdf_event_router_subscribe(sdf_event_router_type_t type,
                                     sdf_event_router_priority_t min_prio,
                                     sdf_event_router_cb cb,
                                     void *ctx,
                                     sdf_event_router_subscriber_t **handle)
{
    if (cb == NULL || handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    sdf_event_router_subscriber_t *sub = calloc(1, sizeof(*sub));
    if (sub == NULL) {
        return ESP_ERR_NO_MEM;
    }

    sub->type = type;
    sub->min_prio = min_prio;
    sub->cb = cb;
    sub->ctx = ctx;
    sub->next = NULL;

    if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(SDF_EVENT_ROUTER_LOCK_WAIT_MS)) == pdTRUE) {
        sub->next = s_state.subscribers_by_type[sub->type];
        s_state.subscribers_by_type[sub->type] = sub;
        xSemaphoreGive(s_state.lock);
    } else {
        free(sub);
        return ESP_ERR_TIMEOUT;
    }

    *handle = sub;
    return ESP_OK;
}

esp_err_t sdf_event_router_unsubscribe(sdf_event_router_subscriber_t *handle)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(SDF_EVENT_ROUTER_LOCK_WAIT_MS)) == pdTRUE) {
        sdf_event_router_subscriber_t *prev = NULL;
        sdf_event_router_subscriber_t *sub = s_state.subscribers_by_type[handle->type];

        while (sub != NULL) {
            if (sub == handle) {
                if (prev == NULL) {
                    s_state.subscribers_by_type[handle->type] = sub->next;
                } else {
                    prev->next = sub->next;
                }
                free(sub);
                xSemaphoreGive(s_state.lock);
                return ESP_OK;
            }
            prev = sub;
            sub = sub->next;
        }
        xSemaphoreGive(s_state.lock);
        return ESP_ERR_NOT_FOUND;
    }

    return ESP_ERR_TIMEOUT;
}

esp_err_t sdf_event_router_emit(const sdf_event_router_event_t *event)
{
    if (event == NULL || !s_state.initialized) {
        return ESP_ERR_INVALID_ARG;
    }

    if (event->priority == SDF_EVENT_ROUTER_PRIO_CRITICAL) {
        sdf_event_router_dispatch_sync(event);
    } else {
        BaseType_t ok = xQueueSend(s_state.queue, event, pdMS_TO_TICKS(100));
        if (ok != pdTRUE) {
            ESP_LOGW(TAG, "Event queue full, dropping event");
            return ESP_ERR_NO_MEM;
        }
    }

    return ESP_OK;
}

esp_err_t sdf_event_router_emit_async(const sdf_event_router_event_t *event)
{
    if (event == NULL || !s_state.initialized) {
        return ESP_ERR_INVALID_ARG;
    }

    if (event->priority == SDF_EVENT_ROUTER_PRIO_CRITICAL) {
        sdf_event_router_dispatch_sync(event);
        return ESP_OK;
    }

    BaseType_t ok = xQueueSend(s_state.queue, event, pdMS_TO_TICKS(100));
    if (ok != pdTRUE) {
        ESP_LOGW(TAG, "Async queue full, dropping event");
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}