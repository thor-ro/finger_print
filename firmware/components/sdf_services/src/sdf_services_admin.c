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

#define SDF_ADMIN_TASK_NAME "sdf_admin"
#define SDF_ADMIN_TASK_STACK 4096
#define SDF_ADMIN_TASK_PRIORITY 5
#define SDF_ADMIN_ACTION_TIMEOUT_MS 10000
#define SDF_ADMIN_IDLE_WAIT_CAP_MS 1000u

static const char *TAG = "sdf_services_admin";

typedef struct {
    QueueHandle_t event_queue;
    bool suspended;
} sdf_admin_task_state_t;

static sdf_admin_task_state_t s_admin_state = {0};

void sdf_admin_task_wake(void) {
    if (s_admin_state.event_queue != NULL) {
        sdf_event_router_event_t evt = {
            .type = SDF_EVENT_ROUTER_INTERNAL_WAKE,
        };
        xQueueSend(s_admin_state.event_queue, &evt, 0);
    }
}

static void sdf_admin_task_event_cb(void *ctx, const sdf_event_router_event_t *event) {
    sdf_admin_task_state_t *state = (sdf_admin_task_state_t *)ctx;
    if (state->event_queue) {
        sdf_event_router_event_t evt_copy = *event;
        xQueueSend(state->event_queue, &evt_copy, 0);
    }
}

esp_err_t sdf_admin_task_init_queue(void) {
    if (s_admin_state.event_queue == NULL) {
        s_admin_state.event_queue = xQueueCreate(10, sizeof(sdf_event_router_event_t));
        if (s_admin_state.event_queue == NULL) {
            ESP_LOGE(TAG, "Failed to create admin task queue");
            return ESP_ERR_NO_MEM;
        }
    }
    return ESP_OK;
}

void sdf_admin_task_deinit_queue(void) {
    if (s_admin_state.event_queue != NULL) {
        QueueHandle_t q = s_admin_state.event_queue;
        s_admin_state.event_queue = NULL;
        vQueueDelete(q);
    }
}

esp_err_t sdf_admin_task_init_subscriptions(void) {
    esp_err_t err;

    err = sdf_event_router_subscribe(SDF_EVENT_ROUTER_ADMIN_ACTION_REQUEST,
                                     SDF_EVENT_ROUTER_PRIO_HIGH,
                                     sdf_admin_task_event_cb, &s_admin_state);
    if (err != ESP_OK) {
        return err;
    }

    err = sdf_event_router_subscribe(SDF_EVENT_ROUTER_BUTTON_PRESS,
                                     SDF_EVENT_ROUTER_PRIO_HIGH,
                                     sdf_admin_task_event_cb, &s_admin_state);
    if (err != ESP_OK) {
        return err;
    }

    /* Deliberately not subscribed to SDF_EVENT_ROUTER_BIOMETRIC_MATCH here:
     * sdf_match_task claims/authorizes any pending admin action itself
     * (sdf_services_try_claim_admin_action, gated on match->permission ==
     * ADMIN) before a BIOMETRIC_MATCH event is ever emitted, so this task
     * never legitimately needs to see that event. An earlier version of
     * this task duplicated the claim here with a weaker check (any
     * user_id > 0, no permission check) that could never actually run in
     * practice since the match task always intercepts pending actions
     * first - but it was a latent privilege-escalation trap for anyone who
     * removed that interception later without noticing this fallback. Do
     * not re-add a BIOMETRIC_MATCH subscription/case here without routing
     * a real permission check through it. */

    /* min_prio is the *lowest* importance this subscriber accepts (the
     * filter is sub->min_prio >= event->priority); POWER_WAKE is emitted at
     * NORMAL and POWER_SLEEP at LOW, so CRITICAL here would silently filter
     * both out. Use LOW to accept every priority for these two types. */
    err = sdf_event_router_subscribe(SDF_EVENT_ROUTER_POWER_WAKE,
                                     SDF_EVENT_ROUTER_PRIO_LOW,
                                     sdf_admin_task_event_cb, &s_admin_state);
    if (err != ESP_OK) {
        return err;
    }

    err = sdf_event_router_subscribe(SDF_EVENT_ROUTER_POWER_SLEEP,
                                     SDF_EVENT_ROUTER_PRIO_LOW,
                                     sdf_admin_task_event_cb, &s_admin_state);
    return err;
}

static void sdf_admin_task_emit_action_complete(sdf_services_admin_action_t action,
                                                 esp_err_t result) {
    sdf_event_router_event_t evt = {
        .type = SDF_EVENT_ROUTER_ADMIN_ACTION_COMPLETE,
        .priority = SDF_EVENT_ROUTER_PRIO_HIGH,
        .timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL),
        .payload.admin_action_complete = {.action = action, .result = result}
    };
    sdf_event_router_emit(&evt, SDF_EVENT_ROUTER_EMIT_TIMEOUT_DEFAULT_MS);
}

/**
 * @brief Executes an admin action directly via the configured admin_action_cb,
 * without entering the pending-admin-action wait. Used by the factory-reset
 * gesture: factory reset is the recovery path for a lost or unreadable Admin
 * fingerprint, so gating it on the fingerprint it recovers from would make it
 * unreachable (device-setup-phase spec). No origin plumbing exists anymore -
 * every other request path follows the ordinary pending-action flow.
 */
void sdf_button_execute_direct(sdf_services_admin_action_t action) {
    ESP_LOGI(TAG, "Admin action direct execution: action=%d", (int)action);

    sdf_services_state_t *s = sdf_services_state();
    if (s == NULL || s->lock == NULL) {
        return;
    }

    if (xSemaphoreTake(s->lock, pdMS_TO_TICKS(SDF_SERVICES_LOCK_WAIT_MS)) != pdTRUE) {
        return;
    }

    sdf_services_admin_action_cb action_cb = s->config.admin_action_cb;
    void *action_ctx = s->config.admin_action_ctx;
    xSemaphoreGive(s->lock);

    if (action_cb != NULL) {
        action_cb(action_ctx, action);
    }
}

/**
 * @brief Shared dispatch body for a remotely-requested admin action.
 *
 * Sets the pending admin action and pulses the pending action LED awaiting
 * the authorizing Admin fingerprint. Every admin-action request path follows
 * this ordinary authorization flow without exception: the old unauthenticated
 * bootstrap bypass is gone (first-time setup runs through the companion-app
 * wizard over the setup phase, and factory reset executes directly at its
 * gesture).
 *
 * Not static: declared in sdf_services_internal.h so it can be driven
 * directly by host (linux target) unit tests - see test_sdf_services.c.
 */
void sdf_services_dispatch_admin_action(sdf_services_admin_action_t action) {
    ESP_LOGI(TAG, "Admin action dispatch: action=%d", (int)action);

    sdf_services_state_t *s = sdf_services_state();
    if (s == NULL || s->lock == NULL) {
        return;
    }

    if (xSemaphoreTake(s->lock, pdMS_TO_TICKS(SDF_SERVICES_LOCK_WAIT_MS)) != pdTRUE) {
        return;
    }

    /* Set the pending action and wait for Admin fingerprint */
    if (s->pending_admin_action == SDF_SERVICES_ADMIN_ACTION_NONE) {
        s->pending_admin_action = action;
        s->pending_admin_action_start_us = esp_timer_get_time();

        sdf_services_pulse_pending_action_led(action);
        ESP_LOGI(TAG, "Pending admin action set: %d", (int)action);
        sdf_admin_task_wake();
    }

    xSemaphoreGive(s->lock);
}

void sdf_button_dispatch_action(sdf_services_admin_action_t action) {
    sdf_services_dispatch_admin_action(action);
}

void sdf_admin_task(void *arg) {
    (void)arg;
    sdf_services_state_t *s = sdf_services_state();

    sdf_platform_time_wdt_add();

    while (true) {
        sdf_platform_time_wdt_reset();

        {
            SDF_LOCK_GUARD(guard, s->lock, SDF_SERVICES_LOCK_WAIT_MS);
            if (guard.acquired == pdTRUE && s->stop_requested) {
                break;
            }
        }

        uint32_t wait_ms = SDF_ADMIN_IDLE_WAIT_CAP_MS;
        if (xSemaphoreTake(s->lock, pdMS_TO_TICKS(SDF_SERVICES_LOCK_WAIT_MS)) == pdTRUE) {
            if (s->pending_admin_action != SDF_SERVICES_ADMIN_ACTION_NONE &&
                s->pending_admin_action_start_us > 0) {
                int64_t now_us = esp_timer_get_time();
                int64_t elapsed_ms = (now_us - s->pending_admin_action_start_us) / 1000LL;
                int64_t remaining_ms = (int64_t)SDF_ADMIN_ACTION_TIMEOUT_MS - elapsed_ms;
                if (remaining_ms < 0) {
                    remaining_ms = 0;
                }
                if (remaining_ms > SDF_ADMIN_IDLE_WAIT_CAP_MS) {
                    remaining_ms = SDF_ADMIN_IDLE_WAIT_CAP_MS;
                }
                wait_ms = (uint32_t)remaining_ms;
            }
            xSemaphoreGive(s->lock);
        }

        sdf_event_router_event_t event;
        if (xQueueReceive(s_admin_state.event_queue, &event, pdMS_TO_TICKS(wait_ms)) == pdTRUE) {
            switch (event.type) {
                case SDF_EVENT_ROUTER_BUTTON_PRESS: {
                    /* Long press: factory reset executes directly, with no
                     * pending-admin-action wait and no Admin fingerprint -
                     * it is the recovery path for a lost/unreadable Admin
                     * fingerprint (device-setup-phase spec). */
                    if (event.payload.button.press_type ==
                        SDF_EVENT_ROUTER_BUTTON_PRESS_LONG) {
                        led_pulse_red();
                        sdf_button_execute_direct(SDF_SERVICES_ADMIN_ACTION_FACTORY_RESET);
                        break;
                    }

                    /* While the device is unclaimed (setup phase armed or
                     * lapsed), every non-long press is the reclaim-and-re-arm
                     * gesture: terminate the current setup connection, re-arm
                     * advertising, restart both timers. No pending admin
                     * action is ever set - no button gesture reaches admin
                     * actions during the setup phase. */
                    if (sdf_services_setup_phase_owns_buttons()) {
                        sdf_services_setup_phase_button_reclaim();
                        break;
                    }

                    switch (event.payload.button.press_type) {
                        case SDF_EVENT_ROUTER_BUTTON_PRESS_DOUBLE:
                            /* Double-click requests the BLE Companion pairing
                             * window; bound only once the setup-completion
                             * latch is set (checked above via owns_buttons).
                             * Single-click has no action left to resolve -
                             * first-time setup runs through the companion-app
                             * wizard, so it does nothing. */
                            sdf_button_dispatch_action(
                                SDF_SERVICES_ADMIN_ACTION_BLE_PAIRING_WINDOW);
                            break;
                        case SDF_EVENT_ROUTER_BUTTON_PRESS_SINGLE:
                            ESP_LOGI(TAG,
                                     "Single click ignored (setup is app-guided; "
                                     "no button setup action exists)");
                            break;
                        default:
                            ESP_LOGW(TAG, "Unknown button press type: %d", (int)event.payload.button.press_type);
                            break;
                    }
                    break;
                }

                case SDF_EVENT_ROUTER_ADMIN_ACTION_REQUEST: {
                    ESP_LOGI(TAG, "Admin action request: %u",
                             (unsigned)event.payload.admin.action);
                    sdf_services_dispatch_admin_action(
                        (sdf_services_admin_action_t)event.payload.admin.action);
                    break;
                }

                case SDF_EVENT_ROUTER_POWER_WAKE:
                    ESP_LOGI(TAG, "Wake event - resuming admin task");
                    s_admin_state.suspended = false;
                    break;

                case SDF_EVENT_ROUTER_POWER_SLEEP:
                    ESP_LOGI(TAG, "Sleep event - suspending admin task");
                    s_admin_state.suspended = true;
                    break;

                case SDF_EVENT_ROUTER_INTERNAL_WAKE:
                    /* Internal wake sentinel to break queue block */
                    break;

                default:
                    break;
            }
        }

        /* Setup-phase timer sweep. The loop wakes at least once a second
         * (SDF_ADMIN_IDLE_WAIT_CAP_MS), which is ample precision for the
         * multi-minute arm window, deadline and idle timers. */
        switch (sdf_services_setup_phase_poll(esp_timer_get_time())) {
            case SDF_SERVICES_SETUP_POLL_WIPE_AND_STOP:
                sdf_services_setup_phase_timeout_wipe();
                break;
            case SDF_SERVICES_SETUP_POLL_DROP_IDLE_CONN:
                sdf_services_setup_phase_idle_drop();
                break;
            case SDF_SERVICES_SETUP_POLL_NONE:
                break;
        }

        /* Check for admin action timeout */
        if (xSemaphoreTake(s->lock, pdMS_TO_TICKS(SDF_SERVICES_LOCK_WAIT_MS)) == pdTRUE) {
            if (s->pending_admin_action != SDF_SERVICES_ADMIN_ACTION_NONE &&
                s->pending_admin_action_start_us > 0) {
                int64_t now_us = esp_timer_get_time();
                if ((now_us - s->pending_admin_action_start_us) >= ((int64_t)SDF_ADMIN_ACTION_TIMEOUT_MS * 1000LL)) {
                    ESP_LOGW(TAG, "Admin Action Timeout. Resetting state.");
                    sdf_services_admin_action_t timed_out = s->pending_admin_action;
                    s->pending_admin_action = SDF_SERVICES_ADMIN_ACTION_NONE;
                    s->pending_admin_action_start_us = 0;
                    /* A remote delete/enroll records its named outcome here
                     * so sdf_app can answer the requesting client with
                     * timeout (companion-user-mgmt). */
                    sdf_services_record_um_action_timeout_locked(timed_out);
                    xSemaphoreGive(s->lock);

                    led_flash_red();
                    if (timed_out == SDF_SERVICES_ADMIN_ACTION_CHANGE_PERMISSION) {
                        sdf_services_complete_permission_change(ESP_ERR_TIMEOUT);
                    }
                    sdf_admin_task_emit_action_complete(timed_out, ESP_ERR_TIMEOUT);
                    continue;
                }
            }
            xSemaphoreGive(s->lock);
        }
    }

    /* Cooperative shutdown requested via sdf_services_stop_tasks(): unwind
     * cleanly instead of being killed from outside. Subscriptions remain in
     * place for the lifetime of the boot; clearing event_queue causes any
     * post-exit callback invocations to discard events safely. */
    sdf_admin_task_deinit_queue();
    sdf_platform_time_wdt_delete();
    {
        SDF_LOCK_GUARD(guard, s->lock, SDF_SERVICES_LOCK_WAIT_MS);
        if (guard.acquired == pdTRUE) {
            s->admin_task = NULL;
        }
    }
    vTaskDelete(NULL);
}