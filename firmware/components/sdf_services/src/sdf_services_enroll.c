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
#define SDF_ENROLL_TASK_PRIORITY 4
#define SDF_ENROLL_IDLE_WAIT_CAP_MS 1000u
#define SDF_ENROLL_RETRY_INTERVAL_MS 200u

static const char *TAG = "sdf_services_enroll";

typedef struct {
    QueueHandle_t event_queue;
    sdf_event_router_subscriber_t *sub_start;
    sdf_event_router_subscriber_t *sub_power_wake;
    sdf_event_router_subscriber_t *sub_power_sleep;
    TaskHandle_t task_handle;
    esp_timer_handle_t retry_timer;
    bool suspended;
} sdf_enroll_task_state_t;

static sdf_enroll_task_state_t s_enroll_state = {0};

void sdf_enroll_task_wake(void) {
    if (s_enroll_state.event_queue != NULL) {
        sdf_event_router_event_t evt = {0};
        xQueueSend(s_enroll_state.event_queue, &evt, 0);
    }
    if (s_enroll_state.task_handle != NULL) {
        xTaskNotifyGive(s_enroll_state.task_handle);
    }
}

static void sdf_enroll_retry_timer_cb(void *arg) {
    (void)arg;
    if (s_enroll_state.event_queue != NULL) {
        sdf_event_router_event_t evt = {
            .type = SDF_EVENT_ROUTER_ENROLLMENT_STEP_RESULT,
        };
        xQueueSend(s_enroll_state.event_queue, &evt, 0);
    }
}

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

    /* min_prio is the *lowest* importance this subscriber accepts (the
     * filter is sub->min_prio >= event->priority); POWER_WAKE is emitted at
     * NORMAL and POWER_SLEEP at LOW, so CRITICAL here would silently filter
     * both out. Use LOW to accept every priority for these two types. */
    sdf_event_router_subscribe(SDF_EVENT_ROUTER_POWER_WAKE,
                               SDF_EVENT_ROUTER_PRIO_LOW,
                               sdf_enroll_task_event_cb, state, &state->sub_power_wake);

    sdf_event_router_subscribe(SDF_EVENT_ROUTER_POWER_SLEEP,
                               SDF_EVENT_ROUTER_PRIO_LOW,
                               sdf_enroll_task_event_cb, state, &state->sub_power_sleep);

    esp_timer_create_args_t timer_args = {
        .callback = sdf_enroll_retry_timer_cb,
        .name = "enroll_retry",
    };
    esp_timer_create(&timer_args, &state->retry_timer);
}

static void sdf_enroll_task_deinit_subscriptions(sdf_enroll_task_state_t *state) {
    if (state->retry_timer) {
        esp_timer_stop(state->retry_timer);
        esp_timer_delete(state->retry_timer);
        state->retry_timer = NULL;
    }
    if (state->sub_start) sdf_event_router_unsubscribe(state->sub_start);
    if (state->sub_power_wake) sdf_event_router_unsubscribe(state->sub_power_wake);
    if (state->sub_power_sleep) sdf_event_router_unsubscribe(state->sub_power_sleep);
    if (state->event_queue) vQueueDelete(state->event_queue);
    state->event_queue = NULL;
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

    /* fp_enroll_step() is a blocking UART round-trip, but the fingerprint
     * owner task now resets its own watchdog entry per dispatched request,
     * and this task resets its own entry while blocked waiting for the
     * reply - no per-call-site reset needed here. */
    fp_set_power(true);
    sdf_fingerprint_op_result_t step_result = fp_enroll_step(
        (sdf_fingerprint_enroll_step_t)current_cmd,
        snapshot_user_id, snapshot_permission);

    if (xSemaphoreTake(s->lock, pdMS_TO_TICKS(SDF_SERVICES_LOCK_WAIT_MS)) != pdTRUE) {
        return;
    }

    sdf_enroll_next_t next = sdf_enrollment_sm_apply_step_result_ex(&s->enrollment, step_result);

    switch (next.action) {
        case SDF_ENROLL_ACT_RETRY_STEP:
            xSemaphoreGive(s->lock);
            ESP_LOGW(TAG, "Enrollment step retry %u", (unsigned)next.retry_count);
            led_enrollment_step_retry();
            if (s_enroll_state.retry_timer) {
                esp_timer_start_once(s_enroll_state.retry_timer,
                                     (uint64_t)SDF_ENROLL_RETRY_INTERVAL_MS * 1000ULL);
            }
            break;

        case SDF_ENROLL_ACT_COMPLETE: {
            xSemaphoreGive(s->lock);
            if (s_enroll_state.retry_timer) {
                esp_timer_stop(s_enroll_state.retry_timer);
            }

            /* Newly enrolled user is now on the sensor; update the persisted
             * cache and write it to NVS before reporting success, so the
             * sensor and the cache/NVS never disagree (see
             * cache-enrolled-user-state). */
            bool persisted = false;
            if (xSemaphoreTake(s->lock, pdMS_TO_TICKS(SDF_SERVICES_LOCK_WAIT_MS)) == pdTRUE) {
                sdf_enrollment_sm_init(&s->enrollment);
                fp_set_keep_power_on(false);

                SDF_SERVICES_BMP_SET(s->enrolled_user_bmp, snapshot_user_id);
                sdf_services_perm_set(s->enrolled_perm_packed, snapshot_user_id,
                                      snapshot_permission);
                persisted = (sdf_services_persist_enrolled_users_locked() == ESP_OK);
                if (!persisted) {
                    /* Undo the in-RAM cache update so it doesn't disagree
                     * with NVS (and, once the rollback delete below lands,
                     * the sensor) after the lock is released. */
                    SDF_SERVICES_BMP_CLEAR(s->enrolled_user_bmp, snapshot_user_id);
                }
                xSemaphoreGive(s->lock);
            }

            if (persisted) {
                led_enrollment_success_green();
                sdf_enroll_task_emit_complete(snapshot_user_id, snapshot_permission);
                break;
            }

            ESP_LOGE(TAG,
                     "Failed to persist enrolled-user cache for user_id=%u "
                     "after retries - rolling back sensor-side enrollment",
                     (unsigned)snapshot_user_id);
            sdf_fingerprint_op_result_t rollback_res = fp_delete_user(snapshot_user_id);
            if (rollback_res != SDF_FINGERPRINT_OP_OK) {
                ESP_LOGE(TAG,
                         "Rollback delete for user_id=%u also failed (%s) - "
                         "sensor print may be orphaned, but the cache "
                         "correctly does not list it as enrolled",
                         (unsigned)snapshot_user_id,
                         sdf_services_fingerprint_result_name(rollback_res));
            }
            led_flash_red();
            sdf_enroll_task_emit_failed(sm_snapshot.state,
                                        (int8_t)SDF_ENROLLMENT_RESULT_FAILED);
            break;
        }

        case SDF_ENROLL_ACT_FAIL: {
            uint8_t step = sm_snapshot.state;
            sdf_enrollment_result_t result = s->enrollment.result;
            xSemaphoreGive(s->lock);
            if (s_enroll_state.retry_timer) {
                esp_timer_stop(s_enroll_state.retry_timer);
            }
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
            xSemaphoreGive(s->lock);
            if (s_enroll_state.retry_timer) {
                esp_timer_start_once(s_enroll_state.retry_timer,
                                     (uint64_t)SDF_ENROLL_RETRY_INTERVAL_MS * 1000ULL);
            }
            break;

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
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));
#ifndef CONFIG_IDF_TARGET_LINUX
        esp_task_wdt_reset();
#endif
    }

    while (true) {
        {
            SDF_LOCK_GUARD(guard, s->lock, SDF_SERVICES_LOCK_WAIT_MS);
            if (guard.acquired == pdTRUE && s->stop_requested) {
                break;
            }
        }

        sdf_event_router_event_t event;
        if (xQueueReceive(s_enroll_state.event_queue, &event,
                          pdMS_TO_TICKS(SDF_ENROLL_IDLE_WAIT_CAP_MS)) == pdTRUE) {
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
                    if (!s_enroll_state.suspended) {
                        sdf_services_run_enrollment_step();
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

                case SDF_EVENT_ROUTER_ENROLLMENT_STEP_RESULT:
                    if (!s_enroll_state.suspended) {
                        sdf_services_run_enrollment_step();
                    }
                    break;

                default:
                    break;
            }
        }

#ifndef CONFIG_IDF_TARGET_LINUX
        esp_task_wdt_reset();
#endif
    }

    /* Cooperative shutdown requested via sdf_services_stop_tasks(): unwind
     * cleanly instead of being killed from outside. */
    sdf_enroll_task_deinit_subscriptions(&s_enroll_state);
#ifndef CONFIG_IDF_TARGET_LINUX
    esp_task_wdt_delete(NULL);
#endif
    {
        SDF_LOCK_GUARD(guard, s->lock, SDF_SERVICES_LOCK_WAIT_MS);
        if (guard.acquired == pdTRUE) {
            s->enroll_task = NULL;
        }
    }
    vTaskDelete(NULL);
}