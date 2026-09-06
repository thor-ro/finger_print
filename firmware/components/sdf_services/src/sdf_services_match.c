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
    /* Snapshot of s->lockout_restore_announce_pending and
     * s->lockout_persist_armed taken under the lock below; both are acted on
     * only after xSemaphoreGive() - like every decision this cycle makes,
     * and in particular because the NVS writes they trigger are blocking
     * I/O that must never run while holding the services lock (the same rule
     * that keeps the UART round-trip outside it). */
    bool announce_restored = false;
    bool success_heal_record = false;
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
        s->lockout_persist_armed = false;
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
    announce_restored = s->lockout_restore_announce_pending;
    xSemaphoreGive(s->lock);

    /* A lockout restored at boot announces itself here rather than from
     * init(): the event router is not guaranteed running until after init
     * returns, but the first match cycle - however it was triggered - is.
     * Announcing unconditionally (even when the restored deadline already
     * expired before this first cycle ran) keeps the CRITICAL/NORMAL pair
     * intact: subscribers must never see a lockout clear they never saw
     * armed, or the companion alarm state and audit trail desync
     * (security-event-unification). The pending flag is only consumed when
     * the emission actually happens - if the router is not ready yet the
     * next cycle retries rather than silently losing the CRITICAL and
     * later emitting an unpaired NORMAL clear (D5). */
    if (announce_restored && sdf_services_is_ready()) {
        sdf_event_router_event_t evt = {
            .type = SDF_EVENT_ROUTER_SECURITY_LOCKOUT,
            .timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL),
            .priority = SDF_EVENT_ROUTER_PRIO_CRITICAL,
            .payload.security.user_id = 0,
            .payload.security.failed_attempts = failed_attempt_threshold,
        };
        sdf_event_router_emit(&evt, SDF_EVENT_ROUTER_EMIT_TIMEOUT_DEFAULT_MS);
        if (xSemaphoreTake(s->lock, pdMS_TO_TICKS(SDF_SERVICES_LOCK_WAIT_MS)) == pdTRUE) {
            s->lockout_restore_announce_pending = false;
            xSemaphoreGive(s->lock);
        }
    }

    if (lockout_cleared) {
        /* The lockout episode ended at its deadline: retire the persisted
         * armed record so a reset no longer re-arms it. Best-effort, outside
         * the lock (blocking I/O); a failed write leaves the record armed,
         * which costs one redundant boot-time lockout, never a stuck one -
         * the RAM deadline is already cleared above. */
        esp_err_t persist_err = sdf_storage_lockout_clear();
        if (persist_err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to persist lockout cleared state: %s",
                     esp_err_to_name(persist_err));
        }
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

    /* Publish sensor readiness from the fingerprint path's own I/O
     * (companion-device-health): the scan answered, or it did not. Never a
     * probe on behalf of a reader. */
    {
        bool ready = false;
        bool publish = false;
        switch (match_result) {
        case SDF_FINGERPRINT_OP_OK:
        case SDF_FINGERPRINT_OP_NO_MATCH:
        case SDF_FINGERPRINT_OP_FULL:
        case SDF_FINGERPRINT_OP_USER_OCCUPIED:
        case SDF_FINGERPRINT_OP_FINGER_OCCUPIED:
            ready = true;
            publish = true;
            break;
        case SDF_FINGERPRINT_OP_TIMEOUT:
        case SDF_FINGERPRINT_OP_IO_ERROR:
        case SDF_FINGERPRINT_OP_PROTOCOL_ERROR:
        case SDF_FINGERPRINT_OP_FAILED:
            ready = false;
            publish = true;
            break;
        default:
            break;
        }
        if (publish && s->config.fingerprint_ready_cb != NULL) {
            s->config.fingerprint_ready_cb(s->config.fingerprint_ready_ctx,
                                           ready);
        }
    }

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
                    /* Record that the armed episode is (about to be)
                     * persisted; only the entry and clear transitions write
                     * NVS - never the per-attempt increments above, which an
                     * attacker pacing scans could otherwise turn into a
                     * flash-wear DoS (persist-biometric-lockout D3). */
                    s->lockout_persist_armed = true;
                }
            }
        } else if (match_result == SDF_FINGERPRINT_OP_OK) {
            s->failed_attempt_count = 0;
            s->failed_attempt_window_start_us = 0;
            /* Matching is structurally impossible while a live lockout is
             * armed (run_match gates on it), so a set flag here can only
             * mean a stale armed record outlived its RAM deadline. Heal it
             * with one write after the lock is released; on every ordinary
             * unlock this snapshot reads false and no NVS traffic happens. */
            success_heal_record = s->lockout_persist_armed;
            s->lockout_persist_armed = false;
        }
        xSemaphoreGive(s->lock);
    }

    if (success_heal_record) {
        esp_err_t persist_err = sdf_storage_lockout_clear();
        if (persist_err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to persist lockout cleared state: %s",
                     esp_err_to_name(persist_err));
        }
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
            /* Persist the armed episode before announcing it. Outside the
             * lock (blocking I/O), and best-effort: a failed write still
             * leaves the lockout fully enforced for this boot - it only
             * means a later reset would drop it, which is logged so the
             * degraded durability is visible (persist-biometric-lockout:
             * "Failed persistence does not break matching"). */
            esp_err_t persist_err = sdf_storage_lockout_save(true);
            if (persist_err != ESP_OK) {
                ESP_LOGW(TAG,
                         "Failed to persist lockout armed state; lockout "
                         "still applies for this boot: %s",
                         esp_err_to_name(persist_err));
            }
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

void sdf_match_task_run_cycle_for_test(void) {
    /* Host (linux) tests drive the exact match-task body synchronously,
     * behind the mock UART's scripted sensor replies, to exercise the
     * lockout persistence transitions without hardware. Not used by
     * production code; runs on the caller's task. */
    sdf_match_task_run_match_cycle(&s_match_state);
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

        /* An armed admin action means the device has ASKED the user to scan,
         * so look actively instead of waiting for the finger-detect edge.
         *
         * That edge is how an idle device wakes, and it arrives while the
         * sensor's main power is off. It does not arrive once a flow holds
         * the sensor powered - an armed gate then saw no wake, ran no match
         * cycle at all, and timed out with the user's finger on the reader.
         * Polling here costs nothing outside a gate: the window is bounded
         * by SDF_ADMIN_ACTION_TIMEOUT_MS. */
        if (!run_match) {
            bool gate_armed = false;
            if (xSemaphoreTake(s->lock, pdMS_TO_TICKS(SDF_SERVICES_LOCK_WAIT_MS)) ==
                pdTRUE) {
                gate_armed =
                    (s->pending_admin_action != SDF_SERVICES_ADMIN_ACTION_NONE);
                xSemaphoreGive(s->lock);
            }
            if (gate_armed) {
                s_match_state.suspended = false;
                run_match = true;
            }
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