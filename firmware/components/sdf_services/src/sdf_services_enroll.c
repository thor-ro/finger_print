#include "sdf_services_internal.h"
#include "sdf_drivers.h"
#include "sdf_platform.h"
#include "sdf_config.h"
#include "sdf_event_router.h"

#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "sdf_event_router.h"

#define SDF_ENROLL_TASK_NAME "sdf_enroll"
#define SDF_ENROLL_TASK_STACK 4096
#define SDF_ENROLL_TASK_PRIORITY 5

static const char *TAG = "sdf_services_enroll";

typedef struct {
    QueueHandle_t event_queue;
    sdf_event_router_subscriber_t *sub_start;
    sdf_event_router_subscriber_t *sub_step_result;
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

    sdf_event_router_subscribe(SDF_EVENT_ROUTER_ENROLLMENT_STEP_RESULT,
                               SDF_EVENT_ROUTER_PRIO_HIGH,
                               sdf_enroll_task_event_cb, state, &state->sub_step_result);

    sdf_event_router_subscribe(SDF_EVENT_ROUTER_POWER_WAKE,
                               SDF_EVENT_ROUTER_PRIO_CRITICAL,
                               sdf_enroll_task_event_cb, state, &state->sub_power_wake);

    sdf_event_router_subscribe(SDF_EVENT_ROUTER_POWER_SLEEP,
                               SDF_EVENT_ROUTER_PRIO_CRITICAL,
                               sdf_enroll_task_event_cb, state, &state->sub_power_sleep);
}

static void sdf_enroll_task_deinit_subscriptions(sdf_enroll_task_state_t *state) {
    if (state->sub_start) sdf_event_router_unsubscribe(state->sub_start);
    if (state->sub_step_result) sdf_event_router_unsubscribe(state->sub_step_result);
    if (state->sub_power_wake) sdf_event_router_unsubscribe(state->sub_power_wake);
    if (state->sub_power_sleep) sdf_event_router_unsubscribe(state->sub_power_sleep);
    if (state->event_queue) vQueueDelete(state->event_queue);
    state->event_queue = NULL;
}

static void sdf_enroll_task_emit_step_complete(uint8_t step, uint8_t status) {
    sdf_event_router_event_t evt = {
        .type = SDF_EVENT_ROUTER_ENROLLMENT_STEP_COMPLETE,
        .priority = SDF_EVENT_ROUTER_PRIO_NORMAL,
        .timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL),
        .payload.enrollment = {.step = step, .status = status}
    };
    sdf_event_router_emit(&evt);
}

static void sdf_enroll_task_emit_complete(uint16_t user_id, uint8_t permission, esp_err_t result) {
    sdf_event_router_event_t evt = {
        .type = SDF_EVENT_ROUTER_ENROLLMENT_COMPLETE,
        .priority = SDF_EVENT_ROUTER_PRIO_HIGH,
        .timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL),
        .payload.enrollment_start = {.user_id = user_id, .action = permission}
    };
    /* Note: Need to add new payload type for enrollment complete */
    (void)evt; /* Suppress unused warning for now */
    ESP_LOGI(TAG, "Enrollment complete: user_id=%u permission=%u result=%s",
             (unsigned)user_id, (unsigned)permission, esp_err_to_name(result));
}

static void sdf_enroll_task_emit_failed(uint16_t user_id, esp_err_t result) {
    ESP_LOGI(TAG, "Enrollment failed: user_id=%u result=%s",
             (unsigned)user_id, esp_err_to_name(result));
}

void sdf_enroll_task(void *arg) {
    (void)arg;
    sdf_services_state_t *s = sdf_services_state();

    sdf_enroll_task_init_subscriptions(&s_enroll_state);
    s_enroll_state.task_handle = xTaskGetCurrentTaskHandle();

    while (true) {
        sdf_event_router_event_t event;
        if (xQueueReceive(s_enroll_state.event_queue, &event, portMAX_DELAY) == pdTRUE) {
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

                    /* Start first enrollment step - this will be handled by the enrollment SM */
                    sdf_services_start_pending_enrollment_if_any();
                    break;
                }

                case SDF_EVENT_ROUTER_ENROLLMENT_STEP_RESULT: {
                    ESP_LOGI(TAG, "Enrollment step result: step=%u status=%u",
                             (unsigned)event.payload.enrollment_step_result.step,
                             (unsigned)event.payload.enrollment_step_result.status);

                    if (xSemaphoreTake(s->lock, pdMS_TO_TICKS(SDF_SERVICES_LOCK_WAIT_MS)) == pdTRUE) {
                        sdf_enrollment_sm_apply_step_result(&s->enrollment,
                                                            event.payload.enrollment_step_result.status);
                        xSemaphoreGive(s->lock);
                    }
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
    }
}