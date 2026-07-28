#include "sdf_event_router.h"
#include "sdkconfig.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#define SDF_EVENT_ROUTER_TASK_NAME "sdf_evt_router"
#define SDF_EVENT_ROUTER_TASK_STACK 3072
#define SDF_EVENT_ROUTER_TASK_PRIORITY 5
#define SDF_EVENT_ROUTER_LOCK_WAIT_MS 100u

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
    sdf_event_router_subscriber_t *subscribers;
    SemaphoreHandle_t lock;
} s_state;

static void sdf_event_router_dispatch_sync(const sdf_event_router_event_t *event)
{
    sdf_event_router_subscriber_t *sub = s_state.subscribers;

    while (sub != NULL) {
        if (sub->type == event->type && sub->min_prio >= event->priority) {
            if (sub->cb != NULL) {
                sub->cb(sub->ctx, event);
            }
        }
        sub = sub->next;
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

    s_state.lock = xSemaphoreCreateMutex();
    if (s_state.lock == NULL) {
        return ESP_ERR_NO_MEM;
    }

    int queue_depth = CONFIG_SDF_EVENT_ROUTER_QUEUE_DEPTH;
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
        sub->next = s_state.subscribers;
        s_state.subscribers = sub;
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
        sdf_event_router_subscriber_t *sub = s_state.subscribers;

        while (sub != NULL) {
            if (sub == handle) {
                if (prev == NULL) {
                    s_state.subscribers = sub->next;
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

    BaseType_t ok = xQueueSend(s_state.queue, event, pdMS_TO_TICKS(100));
    if (ok != pdTRUE) {
        ESP_LOGW(TAG, "Async queue full, dropping event");
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}