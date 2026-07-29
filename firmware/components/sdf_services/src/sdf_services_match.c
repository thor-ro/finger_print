#include "sdf_services_internal.h"
#include "sdf_drivers.h"
#include "sdf_platform.h"
#include "sdf_config.h"
#include "sdf_event_router.h"

#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#ifndef CONFIG_IDF_TARGET_LINUX
#include "esp_task_wdt.h"
#endif
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
    sdf_event_router_subscriber_t *sub_match_req;
    sdf_event_router_subscriber_t *sub_power_wake;
    sdf_event_router_subscriber_t *sub_power_sleep;
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

static void sdf_match_task_init_subscriptions(sdf_match_task_state_t *state) {
    sdf_services_state_t *s = sdf_services_state();
    state->event_queue = s->match_task_queue;

    sdf_event_router_subscribe(SDF_EVENT_ROUTER_BIOMETRIC_MATCH_REQUEST,
                               SDF_EVENT_ROUTER_PRIO_HIGH,
                               sdf_match_task_event_cb, state, &state->sub_match_req);

    sdf_event_router_subscribe(SDF_EVENT_ROUTER_POWER_WAKE,
                               SDF_EVENT_ROUTER_PRIO_CRITICAL,
                               sdf_match_task_event_cb, state, &state->sub_power_wake);

    sdf_event_router_subscribe(SDF_EVENT_ROUTER_POWER_SLEEP,
                               SDF_EVENT_ROUTER_PRIO_CRITICAL,
                               sdf_match_task_event_cb, state, &state->sub_power_sleep);
}

static void sdf_match_task_deinit_subscriptions(sdf_match_task_state_t *state) {
    if (state->sub_match_req) sdf_event_router_unsubscribe(state->sub_match_req);
    if (state->sub_power_wake) sdf_event_router_unsubscribe(state->sub_power_wake);
    if (state->sub_power_sleep) sdf_event_router_unsubscribe(state->sub_power_sleep);
    /* Don't delete queue - it's shared and managed by sdf_services */
    state->event_queue = NULL;
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
    sdf_event_router_emit(&evt);
}

static void sdf_match_task_run_match_cycle(sdf_match_task_state_t *match_state) {
    sdf_services_state_t *s = sdf_services_state();
    sdf_services_unlock_cb unlock_cb = NULL;
    void *unlock_ctx = NULL;
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

    if (s->enrolled_user_count == 0) {
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

    unlock_cb = s->config.unlock_cb;
    unlock_ctx = s->config.unlock_ctx;
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

    if (unlock_cb == NULL) {
        return;
    }

    sdf_fingerprint_match_t match = {0};

#ifndef CONFIG_IDF_TARGET_LINUX
    esp_task_wdt_reset();
#endif

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
            sdf_event_router_emit(&evt);
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
            sdf_event_router_emit(&evt);
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

    int unlock_result = unlock_cb(unlock_ctx, match.user_id);
    if (unlock_result != 0) {
        ESP_LOGW(TAG, "Unlock callback returned %d for user_id=%u", unlock_result,
                 (unsigned)match.user_id);
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
    };
    sdf_event_router_emit(&evt);
}

void sdf_match_task(void *arg) {
    (void)arg;
    sdf_services_state_t *s = sdf_services_state();

    sdf_match_task_init_subscriptions(&s_match_state);
    s_match_state.task_handle = xTaskGetCurrentTaskHandle();

    /* Initial probe and user query on startup */
    bool is_powered = true;

#ifndef CONFIG_IDF_TARGET_LINUX
    esp_task_wdt_add(NULL);
#endif

    /* Wait for initialization to complete */
    while (!sdf_services_is_ready()) {
        vTaskDelay(pdMS_TO_TICKS(10));
#ifndef CONFIG_IDF_TARGET_LINUX
        esp_task_wdt_reset();
#endif
    }

#ifndef CONFIG_IDF_TARGET_LINUX
    esp_task_wdt_reset();
#endif

    /* Fast connectivity check */
    esp_err_t probe_err = fp_probe();

#ifndef CONFIG_IDF_TARGET_LINUX
    esp_task_wdt_reset();
#endif

    /* Check unclaimed state on boot */
    uint16_t users[1];
    uint8_t perms[1];
    size_t count = 0;
    esp_err_t query_err = ESP_FAIL;
    if (probe_err == ESP_OK) {
        query_err = sdf_services_query_users(users, perms, &count, 1);
    } else {
        ESP_LOGW(TAG, "Skipping user query - sensor probe failed");
    }

    if (xSemaphoreTake(s->lock, pdMS_TO_TICKS(SDF_SERVICES_LOCK_WAIT_MS)) == pdTRUE) {
        s->enrolled_user_count = count;
        xSemaphoreGive(s->lock);
    }

#ifndef CONFIG_IDF_TARGET_LINUX
    esp_task_wdt_reset();
#endif

    if (query_err == ESP_OK && count > 0) {
        led_off();
        ESP_LOGI(TAG, "===============================================");
        ESP_LOGI(TAG, "DEVICE STATE: CLAIMED (%zu enrolled users)", count);
        ESP_LOGI(TAG, "===============================================");
        ESP_LOGI(TAG, "AVAILABLE CONFIGURATION ACTIONS:");
        ESP_LOGI(TAG, " -> Short press: Enroll a new standard user.");
        ESP_LOGI(TAG, " -> Triple press: Enroll a new admin user.");
        ESP_LOGI(TAG, " -> Double press: Pair to Nuki Smart Lock (Phase 2).");
        ESP_LOGI(TAG, " -> Hold 3 sec: Join Zigbee Network (Phase 3).");
        ESP_LOGI(TAG, " -> Hold 8 sec: Factory Reset.");
        ESP_LOGI(TAG, "(All actions require your Admin fingerprint validation!)");
        ESP_LOGI(TAG, "===============================================");
    } else {
        if (query_err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to query fingerprint sensor on boot. Error code: %d", query_err);
            ESP_LOGW(TAG, "(Assuming UNCLAIMED state for setup purposes)");
        }

        ESP_LOGI(TAG, "===============================================");
        ESP_LOGI(TAG, "DEVICE STATE: UNCLAIMED (0 enrolled users)");
        ESP_LOGI(TAG, "===============================================");
        ESP_LOGI(TAG, "NEXT STEP (PHASE 1):");
        ESP_LOGI(TAG, " -> Short press Configuration Button once to begin Admin Enrollment.");
        ESP_LOGI(TAG, " -> Place finger 3 times. LED flashes green after each scan.");
        ESP_LOGI(TAG, " -> LED breathes WHITE until the button is pressed.");
        ESP_LOGI(TAG, "===============================================");

        led_breathe_white();
    }

    while (true) {
#ifndef CONFIG_IDF_TARGET_LINUX
        esp_task_wdt_reset();
#endif

        sdf_event_router_event_t event;
        const TickType_t wait_ticks = portMAX_DELAY;
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
}