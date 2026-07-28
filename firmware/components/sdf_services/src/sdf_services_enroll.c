#include "sdf_services_internal.h"
#include "sdf_drivers.h"
#include "sdf_platform.h"
#include "sdf_config.h"
#include "sdf_event_router.h"

#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#define SDF_ENROLL_TASK_NAME "sdf_enroll"
#define SDF_ENROLL_TASK_STACK 4096
#define SDF_ENROLL_TASK_PRIORITY 5
#define SDF_ENROLL_POLL_INTERVAL_MS 100

static const char *TAG = "sdf_services_enroll";

typedef struct {
    QueueHandle_t event_queue;
    sdf_event_router_subscriber_t *sub_start;
    sdf_event_router_subscriber_t *sub_power_wake;
    sdf_event_router_subscriber_t *sub_power_sleep;
    TaskHandle_t task_handle;
    bool suspended;
} sdf_enroll_task_state_t;

static sdf_enroll_task_state_t s_enroll_state = {0};

static void sdf_enroll_task_event_cb(void *ctx, const sdf_event_router_event_t *event) {
    sdf_enroll_task_state_t *state = (sdf_enroll_task_state_t *)ctx;
    if (state->event_queue) {
        sdf_event_router_event_t evt_copy = *event;
        xQueueSend(state->event_queue, &evt_copy, 0);
    }
}

static void sdf_enroll_task_init_subscriptions(sdf_enroll_task_state_t *state) {
    state->event_queue = xQueueCreate(10, sizeof(sdf_event_router_event_t));

    sdf_event_router_subscribe(SDF_EVENT_ROUTER_ENROLLMENT_START,
                               SDF_EVENT_ROUTER_PRIO_HIGH,
                               sdf_enroll_task_event_cb, state, &state->sub_start);

    sdf_event_router_subscribe(SDF_EVENT_ROUTER_POWER_WAKE,
                               SDF_EVENT_ROUTER_PRIO_CRITICAL,
                               sdf_enroll_task_event_cb, state, &state->sub_power_wake);

    sdf_event_router_subscribe(SDF_EVENT_ROUTER_POWER_SLEEP,
                               SDF_EVENT_ROUTER_PRIO_CRITICAL,
                               sdf_enroll_task_event_cb, state, &state->sub_power_sleep);
}



static void sdf_enroll_task_emit_complete(uint16_t user_id, uint8_t permission) {
    sdf_event_router_event_t evt = {
        .type = SDF_EVENT_ROUTER_ENROLLMENT_COMPLETE,
        .priority = SDF_EVENT_ROUTER_PRIO_HIGH,
        .timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL),
        .payload.enrollment_complete = {.user_id = user_id, .permission = permission}
    };
    sdf_event_router_emit(&evt);
    ESP_LOGI(TAG, "Enrollment complete: user_id=%u permission=%u",
             (unsigned)user_id, (unsigned)permission);
}

static void sdf_enroll_task_emit_failed(uint8_t step, int8_t error_code) {
    sdf_event_router_event_t evt = {
        .type = SDF_EVENT_ROUTER_ENROLLMENT_FAILED,
        .priority = SDF_EVENT_ROUTER_PRIO_HIGH,
        .timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL),
        .payload.enrollment_failed = {.step = step, .error_code = error_code}
    };
    sdf_event_router_emit(&evt);
    ESP_LOGE(TAG, "Enrollment failed: step=%u error_code=%d", (unsigned)step, (int)error_code);
}

void sdf_services_run_enrollment_step(void) {
    sdf_services_state_t *s = sdf_services_state();

    if (xSemaphoreTake(s->lock, pdMS_TO_TICKS(SDF_SERVICES_LOCK_WAIT_MS)) != pdTRUE) {
        return;
    }

    if (!sdf_enrollment_sm_is_active(&s->enrollment)) {
        xSemaphoreGive(s->lock);
        return;
    }

    sdf_enrollment_sm_t sm_snapshot = s->enrollment;
    uint8_t current_cmd = sdf_enrollment_sm_current_command(&sm_snapshot);
    uint16_t snapshot_user_id = sm_snapshot.user_id;
    uint8_t snapshot_permission = sm_snapshot.permission;
    xSemaphoreGive(s->lock);

    if (current_cmd == 0) {
        return;
    }

#ifndef CONFIG_IDF_TARGET_LINUX
    esp_task_wdt_reset();
#endif
    fp_set_power(true);
    sdf_fingerprint_op_result_t step_result = fp_enroll_step(
        (sdf_fingerprint_enroll_step_t)current_cmd,
        snapshot_user_id, snapshot_permission);

#ifndef CONFIG_IDF_TARGET_LINUX
    esp_task_wdt_reset();
#endif

    if (xSemaphoreTake(s->lock, pdMS_TO_TICKS(SDF_SERVICES_LOCK_WAIT_MS)) != pdTRUE) {
        return;
    }

    sdf_enroll_next_t next = sdf_enrollment_sm_apply_step_result_ex(&s->enrollment, step_result);

    switch (next.action) {
        case SDF_ENROLL_ACT_RETRY_STEP:
            xSemaphoreGive(s->lock);
            ESP_LOGW(TAG, "Enrollment step retry %u", (unsigned)next.retry_count);
            led_enrollment_step_retry();
            break;

        case SDF_ENROLL_ACT_COMPLETE: {
            xSemaphoreGive(s->lock);
            led_enrollment_success_green();
            sdf_enroll_task_emit_complete(snapshot_user_id, snapshot_permission);
            if (xSemaphoreTake(s->lock, pdMS_TO_TICKS(SDF_SERVICES_LOCK_WAIT_MS)) == pdTRUE) {
                sdf_enrollment_sm_init(&s->enrollment);
                fp_set_keep_power_on(false);
                xSemaphoreGive(s->lock);
            }
            break;
        }

        case SDF_ENROLL_ACT_FAIL: {
            uint8_t step = sm_snapshot.state;
            sdf_enrollment_result_t result = s->enrollment.result;
            xSemaphoreGive(s->lock);
            led_enrollment_failed();
            sdf_enroll_task_emit_failed(step, (int8_t)result);
            if (xSemaphoreTake(s->lock, pdMS_TO_TICKS(SDF_SERVICES_LOCK_WAIT_MS)) == pdTRUE) {
                sdf_enrollment_sm_init(&s->enrollment);
                fp_set_keep_power_on(false);
                xSemaphoreGive(s->lock);
            }
            break;
        }

        case SDF_ENROLL_ACT_EXECUTE_STEP:
        case SDF_ENROLL_ACT_NONE:
        default:
            xSemaphoreGive(s->lock);
            break;
    }
}

void sdf_enroll_task(void *arg) {
    (void)arg;
    sdf_services_state_t *s = sdf_services_state();

#ifndef CONFIG_IDF_TARGET_LINUX
    esp_task_wdt_add(NULL);
#endif

    sdf_enroll_task_init_subscriptions(&s_enroll_state);
    s_enroll_state.task_handle = xTaskGetCurrentTaskHandle();

    /* Wait for initialization to complete */
    while (!sdf_services_is_ready()) {
        vTaskDelay(pdMS_TO_TICKS(10));
#ifndef CONFIG_IDF_TARGET_LINUX
        esp_task_wdt_reset();
#endif
    }

    while (true) {
        sdf_event_router_event_t event;
        if (xQueueReceive(s_enroll_state.event_queue, &event, pdMS_TO_TICKS(100)) == pdTRUE) {
            switch (event.type) {
                case SDF_EVENT_ROUTER_ENROLLMENT_START: {
                    ESP_LOGI(TAG, "Enrollment start: user_id=%u permission=%u",
                             (unsigned)event.payload.enrollment_start.user_id,
                             (unsigned)event.payload.enrollment_start.action);

                    if (xSemaphoreTake(s->lock, pdMS_TO_TICKS(SDF_SERVICES_LOCK_WAIT_MS)) == pdTRUE) {
                        s->request_user_id = event.payload.enrollment_start.user_id;
                        s->request_permission = event.payload.enrollment_start.action;
                        s->enrollment_request_pending = true;
                        sdf_enrollment_sm_init(&s->enrollment);
                        xSemaphoreGive(s->lock);
                    }

                    sdf_services_start_pending_enrollment_if_any();
                    break;
                }

                case SDF_EVENT_ROUTER_POWER_WAKE:
                    ESP_LOGI(TAG, "Wake event - resuming enrollment task");
                    s_enroll_state.suspended = false;
                    break;

                case SDF_EVENT_ROUTER_POWER_SLEEP:
                    ESP_LOGI(TAG, "Sleep event - suspending enrollment task");
                    s_enroll_state.suspended = true;
                    break;

                default:
                    break;
            }
        }

        if (!s_enroll_state.suspended) {
#ifndef CONFIG_IDF_TARGET_LINUX
            esp_task_wdt_reset();
#endif
            sdf_services_run_enrollment_step();
        }

        vTaskDelay(pdMS_TO_TICKS(SDF_ENROLL_POLL_INTERVAL_MS));
    }
}