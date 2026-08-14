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

static const char *TAG = "sdf_event_router";

typedef struct {
    uint8_t type;
    uint8_t min_prio;
    sdf_event_router_cb cb;
    void *ctx;
    uint8_t next;
} sdf_event_router_slot_t;

static struct {
    QueueHandle_t queue;
    TaskHandle_t task;
    bool initialized;
    bool started;
    uint8_t pool_count;
    uint32_t rejected_registrations;
    uint8_t head_by_type[SDF_EVENT_ROUTER_TYPE_COUNT];
    sdf_event_router_slot_t pool[SDF_EVENT_ROUTER_SUBSCRIBER_CAPACITY];
} s_state;

static void sdf_event_router_dispatch_sync(const sdf_event_router_event_t *event)
{
    if (event->type == SDF_EVENT_ROUTER_INTERNAL_WAKE || event->type >= SDF_EVENT_ROUTER_TYPE_COUNT) {
        ESP_LOGE(TAG, "Dropping event with invalid type %d", (int)event->type);
        return;
    }

    uint8_t idx = s_state.head_by_type[event->type];
    while (idx != SDF_EVENT_ROUTER_SLOT_NONE && idx < SDF_EVENT_ROUTER_SUBSCRIBER_CAPACITY) {
        const sdf_event_router_slot_t *slot = &s_state.pool[idx];
        if (slot->min_prio >= event->priority && slot->cb != NULL) {
            slot->cb(slot->ctx, event);
        }
        idx = slot->next;
    }
}

static void sdf_event_router_task(void *arg)
{
    (void)arg;
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
    memset(s_state.head_by_type, SDF_EVENT_ROUTER_SLOT_NONE, sizeof(s_state.head_by_type));

    uint32_t queue_depth = sdf_config_get()->event_router_queue_depth;
    s_state.queue = xQueueCreate(queue_depth, sizeof(sdf_event_router_event_t));
    if (s_state.queue == NULL) {
        return ESP_ERR_NO_MEM;
    }

    s_state.initialized = true;
    ESP_LOGI(TAG, "Event router initialized (queue depth=%u, capacity=%u)",
             (unsigned)queue_depth, (unsigned)SDF_EVENT_ROUTER_SUBSCRIBER_CAPACITY);
    return ESP_OK;
}

esp_err_t sdf_event_router_subscribe(sdf_event_router_type_t type,
                                     sdf_event_router_priority_t min_prio,
                                     sdf_event_router_cb cb,
                                     void *ctx)
{
    if (cb == NULL || type == SDF_EVENT_ROUTER_INTERNAL_WAKE || type >= SDF_EVENT_ROUTER_TYPE_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_state.initialized || s_state.started) {
        ESP_LOGE(TAG, "Cannot subscribe: router %s",
                 !s_state.initialized ? "not initialized" : "already started");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_state.pool_count >= SDF_EVENT_ROUTER_SUBSCRIBER_CAPACITY) {
        s_state.rejected_registrations++;
        ESP_LOGE(TAG, "Subscriber pool exhausted (capacity=%u, rejected=%u)",
                 (unsigned)SDF_EVENT_ROUTER_SUBSCRIBER_CAPACITY,
                 (unsigned)s_state.rejected_registrations);
        return ESP_ERR_NO_MEM;
    }

    uint8_t slot_idx = s_state.pool_count++;
    sdf_event_router_slot_t *slot = &s_state.pool[slot_idx];
    slot->type = (uint8_t)type;
    slot->min_prio = (uint8_t)min_prio;
    slot->cb = cb;
    slot->ctx = ctx;
    slot->next = s_state.head_by_type[type];
    s_state.head_by_type[type] = slot_idx;

    return ESP_OK;
}

esp_err_t sdf_event_router_start(void)
{
    if (!s_state.initialized) {
        ESP_LOGE(TAG, "Cannot start router before init");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_state.started) {
        return ESP_OK;
    }

    if (s_state.rejected_registrations > 0) {
        ESP_LOGE(TAG, "Cannot start router: %u registrations were rejected due to capacity",
                 (unsigned)s_state.rejected_registrations);
        return ESP_FAIL;
    }

    BaseType_t task_ok = xTaskCreate(sdf_event_router_task,
                                     SDF_EVENT_ROUTER_TASK_NAME,
                                     SDF_EVENT_ROUTER_TASK_STACK,
                                     NULL,
                                     SDF_EVENT_ROUTER_TASK_PRIORITY,
                                     &s_state.task);
    if (task_ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create event router task");
        return ESP_FAIL;
    }

    s_state.started = true;
    ESP_LOGI(TAG, "Event router started: %u/%u subscribers registered",
             (unsigned)s_state.pool_count,
             (unsigned)SDF_EVENT_ROUTER_SUBSCRIBER_CAPACITY);
    return ESP_OK;
}

esp_err_t sdf_event_router_emit(const sdf_event_router_event_t *event)
{
    if (event == NULL || !s_state.initialized ||
        event->type == SDF_EVENT_ROUTER_INTERNAL_WAKE ||
        event->type >= SDF_EVENT_ROUTER_TYPE_COUNT) {
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

esp_err_t sdf_event_router_emit_nonblocking(const sdf_event_router_event_t *event)
{
    if (event == NULL || !s_state.initialized ||
        event->type == SDF_EVENT_ROUTER_INTERNAL_WAKE ||
        event->type >= SDF_EVENT_ROUTER_TYPE_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    if (event->priority == SDF_EVENT_ROUTER_PRIO_CRITICAL) {
        sdf_event_router_dispatch_sync(event);
    } else {
        BaseType_t ok = xQueueSend(s_state.queue, event, 0);
        if (ok != pdTRUE) {
            ESP_LOGW(TAG, "Event queue full (non-blocking), dropping event type=%d", (int)event->type);
            return ESP_ERR_NO_MEM;
        }
    }

    return ESP_OK;
}

#ifdef CONFIG_IDF_TARGET_LINUX
void sdf_event_router_reset_for_test(void)
{
    if (s_state.task != NULL) {
        vTaskDelete(s_state.task);
        s_state.task = NULL;
    }
    if (s_state.queue != NULL) {
        vQueueDelete(s_state.queue);
        s_state.queue = NULL;
    }
    memset(&s_state, 0, sizeof(s_state));
    memset(s_state.head_by_type, SDF_EVENT_ROUTER_SLOT_NONE, sizeof(s_state.head_by_type));
}
#endif