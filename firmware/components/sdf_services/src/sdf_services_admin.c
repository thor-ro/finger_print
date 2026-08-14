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

#define SDF_ADMIN_TASK_NAME "sdf_admin"
#define SDF_ADMIN_TASK_STACK 4096
#define SDF_ADMIN_TASK_PRIORITY 5
#define SDF_ADMIN_ACTION_TIMEOUT_MS 10000
#define SDF_ADMIN_IDLE_WAIT_CAP_MS 1000u

static const char *TAG = "sdf_services_admin";

typedef struct {
    QueueHandle_t event_queue;
    sdf_event_router_subscriber_t *sub_action_req;
    sdf_event_router_subscriber_t *sub_button_press;
    sdf_event_router_subscriber_t *sub_power_wake;
    sdf_event_router_subscriber_t *sub_power_sleep;
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

static void sdf_admin_task_init_subscriptions(sdf_admin_task_state_t *state) {
    state->event_queue = xQueueCreate(10, sizeof(sdf_event_router_event_t));

    sdf_event_router_subscribe(SDF_EVENT_ROUTER_ADMIN_ACTION_REQUEST,
                               SDF_EVENT_ROUTER_PRIO_HIGH,
                               sdf_admin_task_event_cb, state, &state->sub_action_req);

    sdf_event_router_subscribe(SDF_EVENT_ROUTER_BUTTON_PRESS,
                               SDF_EVENT_ROUTER_PRIO_HIGH,
                               sdf_admin_task_event_cb, state, &state->sub_button_press);

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
    sdf_event_router_subscribe(SDF_EVENT_ROUTER_POWER_WAKE,
                               SDF_EVENT_ROUTER_PRIO_LOW,
                               sdf_admin_task_event_cb, state, &state->sub_power_wake);

    sdf_event_router_subscribe(SDF_EVENT_ROUTER_POWER_SLEEP,
                               SDF_EVENT_ROUTER_PRIO_LOW,
                               sdf_admin_task_event_cb, state, &state->sub_power_sleep);
}

static void sdf_admin_task_deinit_subscriptions(sdf_admin_task_state_t *state) {
    if (state->sub_action_req) sdf_event_router_unsubscribe(state->sub_action_req);
    if (state->sub_button_press) sdf_event_router_unsubscribe(state->sub_button_press);
    if (state->sub_power_wake) sdf_event_router_unsubscribe(state->sub_power_wake);
    if (state->sub_power_sleep) sdf_event_router_unsubscribe(state->sub_power_sleep);
    if (state->event_queue) vQueueDelete(state->event_queue);
    state->event_queue = NULL;
}

static void sdf_admin_task_emit_action_complete(sdf_services_admin_action_t action,
                                                 esp_err_t result) {
    sdf_event_router_event_t evt = {
        .type = SDF_EVENT_ROUTER_ADMIN_ACTION_COMPLETE,
        .priority = SDF_EVENT_ROUTER_PRIO_HIGH,
        .timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL),
        .payload.admin_action_complete = {.action = action, .result = result}
    };
    sdf_event_router_emit(&evt);
}

/**
 * @brief Resolve single-click's action dynamically at press consumption time based on
 * the device's current setup state, per the "State-Dependent Single-Click
 * Setup Action" requirement:
 *   - Unclaimed (no enrolled users)              -> ENROLL
 *   - Claimed, Nuki not yet paired                -> NUKI_PAIR
 *   - Claimed, Nuki already paired (setup complete) -> ENROLL
 */
sdf_services_admin_action_t sdf_button_resolve_single_click_action(void) {
    sdf_services_setup_state_t setup_state = sdf_services_get_setup_state();

    if (setup_state == SDF_SERVICES_SETUP_STATE_CLAIMED_INCOMPLETE) {
        return SDF_SERVICES_ADMIN_ACTION_NUKI_PAIR;
    }
    return SDF_SERVICES_ADMIN_ACTION_ENROLL;
}

/**
 * @brief Shared dispatch body for a resolved admin action, regardless of
 * whether the action was fixed at registration time (double-click, holds)
 * or resolved dynamically at press time (single-click).
 *
 * Consults sdf_services_try_bootstrap_admin_action() passing local-physical
 * origin to determine whether the action can bypass admin-fingerprint authorization
 * (e.g. on an unclaimed device with 0 enrolled users). If not bypassed, sets the
 * pending admin action and pulses the pending action LED awaiting admin fingerprint.
 *
 * Not static: declared in sdf_services_internal.h so it can be driven
 * directly by host (linux target) unit tests without the real iot_button
 * GPIO plumbing - see test_sdf_services.c.
 */
void sdf_button_dispatch_action(sdf_services_admin_action_t action) {
    ESP_LOGI(TAG, "Admin action dispatch: action=%d", (int)action);

    if (sdf_services_try_bootstrap_admin_action(action, SDF_SERVICES_ADMIN_ORIGIN_LOCAL_PHYSICAL)) {
        return;
    }

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

void sdf_admin_task(void *arg) {
    (void)arg;
    sdf_services_state_t *s = sdf_services_state();

    sdf_admin_task_init_subscriptions(&s_admin_state);

    while (true) {
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
                    sdf_services_admin_action_t action = SDF_SERVICES_ADMIN_ACTION_NONE;
                    switch (event.payload.button.press_type) {
                        case SDF_EVENT_ROUTER_BUTTON_PRESS_SINGLE:
                            action = sdf_button_resolve_single_click_action();
                            break;
                        case SDF_EVENT_ROUTER_BUTTON_PRESS_DOUBLE:
                            action = SDF_SERVICES_ADMIN_ACTION_BLE_PAIRING_WINDOW;
                            break;
                        case SDF_EVENT_ROUTER_BUTTON_PRESS_LONG:
                            action = SDF_SERVICES_ADMIN_ACTION_FACTORY_RESET;
                            break;
                        default:
                            ESP_LOGW(TAG, "Unknown button press type: %d", (int)event.payload.button.press_type);
                            break;
                    }
                    if (action != SDF_SERVICES_ADMIN_ACTION_NONE) {
                        sdf_button_dispatch_action(action);
                    }
                    break;
                }

                case SDF_EVENT_ROUTER_ADMIN_ACTION_REQUEST: {
                    ESP_LOGI(TAG, "Admin action request: %u", (unsigned)event.payload.admin.action);
                    sdf_button_dispatch_action((sdf_services_admin_action_t)event.payload.admin.action);
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
     * cleanly instead of being killed from outside. */
    sdf_admin_task_deinit_subscriptions(&s_admin_state);
    {
        SDF_LOCK_GUARD(guard, s->lock, SDF_SERVICES_LOCK_WAIT_MS);
        if (guard.acquired == pdTRUE) {
            s->admin_task = NULL;
        }
    }
    vTaskDelete(NULL);
}