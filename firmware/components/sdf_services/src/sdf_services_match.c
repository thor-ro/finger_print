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

#define SDF_MATCH_TASK_NAME "sdf_match"
#define SDF_MATCH_TASK_STACK 4096
#define SDF_MATCH_TASK_PRIORITY 5

static const char *TAG = "sdf_services_match";

typedef struct {
    QueueHandle_t event_queue;
    TaskHandle_t task_handle;
    bool suspended;
    bool pending_match_request;
} sdf_match_task_state_t;

static sdf_match_task_state_t s_match_state = {0};

static void sdf_match_task_event_cb(void *ctx, const sdf_event_router_event_t *event) {
    sdf_match_task_state_t *state = (sdf_match_task_state_t *)ctx;
    if (state->event_queue) {
        sdf_event_router_event_t evt_copy = *event;
        xQueueSend(state->event_queue, &evt_copy, 0);
    }
}

esp_err_t sdf_match_task_init_queue(void) {
    sdf_services_state_t *s = sdf_services_state();
    if (s_match_state.event_queue == NULL) {
        if (s->match_task_queue == NULL) {
            s->match_task_queue = xQueueCreate(10, sizeof(sdf_event_router_event_t));
            if (s->match_task_queue == NULL) {
                ESP_LOGE(TAG, "Failed to create match task queue");
                return ESP_ERR_NO_MEM;
            }
        }
        s_match_state.event_queue = s->match_task_queue;
    }
    return ESP_OK;
}

void sdf_match_task_deinit_queue(void) {
    sdf_services_state_t *s = sdf_services_state();
    s_match_state.event_queue = NULL;
    /* Clear the shared handle before deleting it: sdf_services_stop_tasks()
     * also calls this after force-deleting a task that may already be partway
     * through its own cooperative deinit, and a check-then-delete would let
     * both contexts reach vQueueDelete() on the same handle. */
    QueueHandle_t q = s->match_task_queue;
    s->match_task_queue = NULL;
    if (q != NULL) {
        vQueueDelete(q);
    }
}

esp_err_t sdf_match_task_init_subscriptions(void) {
    esp_err_t err;

    err = sdf_event_router_subscribe(SDF_EVENT_ROUTER_BIOMETRIC_MATCH_REQUEST,
                                     SDF_EVENT_ROUTER_PRIO_HIGH,
                                     sdf_match_task_event_cb, &s_match_state);
    if (err != ESP_OK) {
        return err;
    }

    /* min_prio is the *lowest* importance this subscriber accepts (the
     * filter is sub->min_prio >= event->priority); POWER_WAKE is emitted at
     * NORMAL and POWER_SLEEP at LOW, so CRITICAL here would silently filter
     * both out. Use LOW to accept every priority for these two types. */
    err = sdf_event_router_subscribe(SDF_EVENT_ROUTER_POWER_WAKE,
                                     SDF_EVENT_ROUTER_PRIO_LOW,
                                     sdf_match_task_event_cb, &s_match_state);
    if (err != ESP_OK) {
        return err;
    }

    err = sdf_event_router_subscribe(SDF_EVENT_ROUTER_POWER_SLEEP,
                                     SDF_EVENT_ROUTER_PRIO_LOW,
                                     sdf_match_task_event_cb, &s_match_state);
    return err;
}

static void sdf_match_emit_lockout_cleared(void) {
    if (!sdf_services_is_ready()) {
        return;
    }
    sdf_event_router_event_t evt = {
        .type = SDF_EVENT_ROUTER_SECURITY_LOCKOUT,
        .timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL),
        .priority = SDF_EVENT_ROUTER_PRIO_NORMAL,
        .payload.security.user_id = 0,
        .payload.security.failed_attempts = 0,
    };
    sdf_event_router_emit(&evt, SDF_EVENT_ROUTER_EMIT_TIMEOUT_DEFAULT_MS);
}

static void sdf_match_task_run_match_cycle(sdf_match_task_state_t *match_state) {
    sdf_services_state_t *s = sdf_services_state();
    uint32_t cooldown_ms;
    uint32_t failed_attempt_threshold;
    uint32_t failed_attempt_window_ms;
    uint32_t lockout_duration_ms;
    int64_t now_us = esp_timer_get_time();
    bool lockout_cleared = false;
    bool run_match = false;

    if (xSemaphoreTake(s->lock, pdMS_TO_TICKS(SDF_SERVICES_LOCK_WAIT_MS)) != pdTRUE) {
        return;
    }

    if (sdf_services_enrolled_user_count(s->enrolled_user_bmp) == 0) {
        xSemaphoreGive(s->lock);
        return;
    }

    if (s->lockout_until_us > 0 && now_us >= s->lockout_until_us) {
        s->lockout_until_us = 0;
        s->failed_attempt_count = 0;
        s->failed_attempt_window_start_us = 0;
        lockout_cleared = true;
    }

    if (now_us >= s->match_cooldown_until_us &&
        !sdf_enrollment_sm_is_active(&s->enrollment) &&
        !s->enrollment_request_pending &&
        now_us >= s->lockout_until_us) {
        run_match = true;
    }

    cooldown_ms = s->config.match_cooldown_ms;
    failed_attempt_threshold = s->config.failed_attempt_threshold;
    failed_attempt_window_ms = s->config.failed_attempt_window_ms;
    lockout_duration_ms = s->config.lockout_duration_ms;
    xSemaphoreGive(s->lock);

    if (lockout_cleared) {
        sdf_match_emit_lockout_cleared();
    }

    if (!run_match) {
        return;
    }

    sdf_fingerprint_match_t match = {0};

    /* fp_match_1n() is a blocking UART round-trip (up to
     * CONFIG_SDF_FP_RESPONSE_TIMEOUT_MS), but the fingerprint owner task now
     * resets its own watchdog entry per dispatched request, and this task
     * resets its own entry while blocked waiting for the reply - no
     * per-call-site reset needed here. */
    sdf_fingerprint_op_result_t match_result = fp_match_1n(&match);

    uint32_t failed_attempts_after = 0;
    bool emit_failed_attempt = false;
    bool emit_lockout = false;

    if (xSemaphoreTake(s->lock, pdMS_TO_TICKS(SDF_SERVICES_LOCK_WAIT_MS)) == pdTRUE) {
        s->match_cooldown_until_us = now_us + ((int64_t)cooldown_ms * 1000LL);

        if (match_result == SDF_FINGERPRINT_OP_NO_MATCH ||
            match_result == SDF_FINGERPRINT_OP_TIMEOUT) {
            if (match_result == SDF_FINGERPRINT_OP_NO_MATCH) {
                if (s->failed_attempt_window_start_us == 0 ||
                    (now_us - s->failed_attempt_window_start_us) >
                        ((int64_t)failed_attempt_window_ms * 1000LL)) {
                    s->failed_attempt_window_start_us = now_us;
                    s->failed_attempt_count = 0;
                }

                s->failed_attempt_count++;
                failed_attempts_after = s->failed_attempt_count;
                emit_failed_attempt = true;

                if (s->failed_attempt_count >= failed_attempt_threshold) {
                    s->lockout_until_us = now_us +
                        ((int64_t)lockout_duration_ms * 1000LL);
                    s->failed_attempt_count = 0;
                    s->failed_attempt_window_start_us = 0;
                    emit_lockout = true;
                }
            }
        } else if (match_result == SDF_FINGERPRINT_OP_OK) {
            s->failed_attempt_count = 0;
            s->failed_attempt_window_start_us = 0;
        }
        xSemaphoreGive(s->lock);
    }

    if (match_result == SDF_FINGERPRINT_OP_NO_MATCH ||
        match_result == SDF_FINGERPRINT_OP_TIMEOUT) {
        if (emit_failed_attempt) {
            if (!sdf_services_is_ready()) {
                return;
            }
            sdf_event_router_event_t evt = {
                .type = SDF_EVENT_ROUTER_BIOMETRIC_MATCH_FAILED,
                .timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL),
                .priority = SDF_EVENT_ROUTER_PRIO_HIGH,
                .payload.security.user_id = 0,
                .payload.security.failed_attempts = failed_attempts_after,
            };
            sdf_event_router_emit(&evt, SDF_EVENT_ROUTER_EMIT_TIMEOUT_DEFAULT_MS);
        }

        if (emit_lockout) {
            if (!sdf_services_is_ready()) {
                return;
            }
            sdf_event_router_event_t evt = {
                .type = SDF_EVENT_ROUTER_SECURITY_LOCKOUT,
                .timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL),
                .priority = SDF_EVENT_ROUTER_PRIO_CRITICAL,
                .payload.security.user_id = 0,
                .payload.security.failed_attempts = failed_attempt_threshold,
            };
            sdf_event_router_emit(&evt, SDF_EVENT_ROUTER_EMIT_TIMEOUT_DEFAULT_MS);
        }
        return;
    }

    if (match_result != SDF_FINGERPRINT_OP_OK) {
        ESP_LOGW(TAG, "Fingerprint match error: %s",
                 sdf_services_fingerprint_result_name(match_result));
        return;
    }

    ESP_LOGI(TAG, "Fingerprint match user_id=%u permission=%u",
             (unsigned)match.user_id, (unsigned)match.permission);

    if (sdf_services_try_claim_admin_action(&match)) {
        return;
    }

    if (!sdf_services_is_ready()) {
        return;
    }
    sdf_event_router_event_t evt = {
        .type = SDF_EVENT_ROUTER_BIOMETRIC_MATCH,
        .timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL),
        .priority = SDF_EVENT_ROUTER_PRIO_HIGH,
        .payload.biometric.user_id = match.user_id,
        .payload.biometric.confidence = 100,
        .payload.biometric.permission = match.permission,
    };
    sdf_event_router_emit(&evt, SDF_EVENT_ROUTER_EMIT_TIMEOUT_DEFAULT_MS);
}

void sdf_match_task(void *arg) {
    (void)arg;
    sdf_services_state_t *s = sdf_services_state();

    s_match_state.task_handle = xTaskGetCurrentTaskHandle();

    /* Initial probe and user query on startup */
    bool is_powered = true;

    sdf_platform_time_wdt_add();

    /* Wait for initialization to complete */
    while (!sdf_services_is_ready()) {
        vTaskDelay(pdMS_TO_TICKS(10));
        sdf_platform_time_wdt_reset();
    }

    /* Fast connectivity check. fp_probe() is a blocking UART round-trip; see
     * the comment above fp_match_1n() - no per-call-site reset needed. Kept
     * as a boot-time sensor health/connectivity indicator even though it's
     * no longer paired with a user query below - the enrolled-user cache is
     * already authoritative at this point (loaded synchronously from NVS in
     * sdf_services_init(), before this task was even created), so a probe
     * failure here doesn't change device-state reporting, only the log. */
    esp_err_t probe_err = fp_probe();
    if (probe_err != ESP_OK) {
        ESP_LOGW(TAG, "Sensor probe failed on boot: %s", esp_err_to_name(probe_err));
    }

    /* No boot-time sensor query needed: the enrolled-user cache was already
     * loaded synchronously from NVS in sdf_services_init(), before any task
     * (including this one) was started - see cache-enrolled-user-state. */
    size_t count = 0;
    if (xSemaphoreTake(s->lock, pdMS_TO_TICKS(SDF_SERVICES_LOCK_WAIT_MS)) == pdTRUE) {
        count = sdf_services_enrolled_user_count(s->enrolled_user_bmp);
        xSemaphoreGive(s->lock);
    }

    sdf_platform_time_wdt_reset();

    if (count > 0) {
        led_off();
        ESP_LOGI(TAG, "===============================================");
        ESP_LOGI(TAG, "DEVICE STATE: CLAIMED (%zu enrolled users)", count);
        ESP_LOGI(TAG, "===============================================");
        ESP_LOGI(TAG, "AVAILABLE CONFIGURATION ACTIONS (via Web Companion app):");
        ESP_LOGI(TAG, " -> Enroll a new standard user or admin (Enrollment panel).");
        ESP_LOGI(TAG, " -> Double press: open BLE Companion pairing window.");
        ESP_LOGI(TAG, " -> Request Nuki re-pair / Zigbee join from the dashboard.");
        ESP_LOGI(TAG, " -> Hold 8 sec: Factory Reset.");
        ESP_LOGI(TAG, "(App actions require your Admin fingerprint validation!)");
        ESP_LOGI(TAG, "===============================================");
    } else {
        ESP_LOGI(TAG, "===============================================");
        ESP_LOGI(TAG, "DEVICE STATE: UNCLAIMED (0 enrolled users)");
        ESP_LOGI(TAG, "===============================================");
        ESP_LOGI(TAG, "NEXT STEP:");
        ESP_LOGI(TAG, " -> Connect with the Web Companion app to run the setup wizard");
        ESP_LOGI(TAG, "    (Admin enrolment, account registration, Nuki pairing).");
        ESP_LOGI(TAG, " -> If advertising has lapsed, press the Configuration Button once");
        ESP_LOGI(TAG, "    to re-arm the setup phase.");
        ESP_LOGI(TAG, "===============================================");

        led_breathe_white();
    }

    while (true) {
        {
            SDF_LOCK_GUARD(guard, s->lock, SDF_SERVICES_LOCK_WAIT_MS);
            if (guard.acquired == pdTRUE && s->stop_requested) {
                break;
            }
        }

        sdf_platform_time_wdt_reset();

        sdf_event_router_event_t event;
        /* Bounded wait (matches sdf_enroll_task) so the loop
         * always comes back around to sdf_platform_time_wdt_reset() above even when
         * idle. A 15s TWDT is configured in sdf_app_init; blocking here
         * forever with portMAX_DELAY would starve the reset and panic the
         * device the first time no event arrives for 15s. It also bounds how
         * long sdf_services_stop_tasks() has to wait for the stop_requested
         * check above to be noticed. */
        const TickType_t wait_ticks = pdMS_TO_TICKS(100);
        bool run_match = false;

        if (xQueueReceive(s_match_state.event_queue, &event, wait_ticks) == pdTRUE) {
            switch (event.type) {
                case SDF_EVENT_ROUTER_BIOMETRIC_MATCH_REQUEST:
                    ESP_LOGD(TAG, "Match request received");
                    s_match_state.pending_match_request = true;
                    s_match_state.suspended = false;
                    run_match = true;
                    break;
                case SDF_EVENT_ROUTER_POWER_WAKE:
                    ESP_LOGI(TAG, "Wake event - resuming match task");
                    s_match_state.suspended = false;
                    run_match = true;
                    break;
                case SDF_EVENT_ROUTER_POWER_SLEEP:
                    ESP_LOGI(TAG, "Sleep event - suspending match task");
                    s_match_state.suspended = true;
                    break;
                default:
                    break;
            }
        }

        if (ulTaskNotifyTake(pdFALSE, 0) > 0) {
            ESP_LOGD(TAG, "Wake ISR notification received");
            s_match_state.pending_match_request = true;
            s_match_state.suspended = false;
            run_match = true;
        }

        if (s_match_state.suspended && is_powered) {
            led_off();
            fp_set_power(false);
            is_powered = false;
        } else if (!s_match_state.suspended && !is_powered) {
            fp_set_power(true);
            is_powered = true;
            vTaskDelay(pdMS_TO_TICKS(200));
        }

        if (!s_match_state.suspended && run_match) {
            sdf_match_task_run_match_cycle(&s_match_state);
        }
    }

    /* Cooperative shutdown requested via sdf_services_stop_tasks(): unwind
     * cleanly instead of being killed from outside. Subscriptions remain in
     * place for the lifetime of the boot; clearing event_queue causes any
     * post-exit callback invocations to discard events safely. */
    sdf_match_task_deinit_queue();
    sdf_platform_time_wdt_delete();
    {
        SDF_LOCK_GUARD(guard, s->lock, SDF_SERVICES_LOCK_WAIT_MS);
        if (guard.acquired == pdTRUE) {
            s->match_task = NULL;
        }
    }
    vTaskDelete(NULL);
}