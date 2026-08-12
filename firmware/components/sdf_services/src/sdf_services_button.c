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

#ifndef CONFIG_IDF_TARGET_LINUX
#include "button_gpio.h"
#include "iot_button.h"
#else
#include "sdf_mock_linux_gpio.h"
#endif

#define SDF_BUTTON_TASK_NAME "sdf_btn"
#define SDF_BUTTON_TASK_STACK 3072
#define SDF_BUTTON_TASK_PRIORITY 4

static const char *TAG = "sdf_services_button";

typedef enum {
    SDF_BUTTON_PRESS_SINGLE = 1,
    SDF_BUTTON_PRESS_DOUBLE = 2,
    SDF_BUTTON_PRESS_TRIPLE = 3,
    SDF_BUTTON_PRESS_LONG_3S = 4,
    SDF_BUTTON_PRESS_LONG_8S = 5,
} sdf_button_press_type_t;

typedef struct {
    QueueHandle_t event_queue;
    sdf_event_router_subscriber_t *sub_power_wake;
    sdf_event_router_subscriber_t *sub_power_sleep;
    TaskHandle_t task_handle;
    bool suspended;
#ifndef CONFIG_IDF_TARGET_LINUX
    button_handle_t btn_handle;
#endif
} sdf_button_task_state_t;

static sdf_button_task_state_t s_button_state = {0};

static void sdf_button_task_event_cb(void *ctx, const sdf_event_router_event_t *event) {
    sdf_button_task_state_t *state = (sdf_button_task_state_t *)ctx;
    if (state->event_queue) {
        sdf_event_router_event_t evt_copy = *event;
        xQueueSend(state->event_queue, &evt_copy, 0);
    }
}

static void sdf_button_task_init_subscriptions(sdf_button_task_state_t *state) {
    state->event_queue = xQueueCreate(10, sizeof(sdf_event_router_event_t));

    /* min_prio is the *lowest* importance this subscriber accepts (the
     * filter is sub->min_prio >= event->priority); POWER_WAKE is emitted at
     * NORMAL and POWER_SLEEP at LOW, so CRITICAL here would silently filter
     * both out. Use LOW to accept every priority for these two types. */
    sdf_event_router_subscribe(SDF_EVENT_ROUTER_POWER_WAKE,
                               SDF_EVENT_ROUTER_PRIO_LOW,
                               sdf_button_task_event_cb, state, &state->sub_power_wake);

    sdf_event_router_subscribe(SDF_EVENT_ROUTER_POWER_SLEEP,
                               SDF_EVENT_ROUTER_PRIO_LOW,
                               sdf_button_task_event_cb, state, &state->sub_power_sleep);
}

static void sdf_button_task_deinit_subscriptions(sdf_button_task_state_t *state) {
    if (state->sub_power_wake) sdf_event_router_unsubscribe(state->sub_power_wake);
    if (state->sub_power_sleep) sdf_event_router_unsubscribe(state->sub_power_sleep);
    if (state->event_queue) vQueueDelete(state->event_queue);
    state->event_queue = NULL;
}

static void sdf_button_task_emit_button_press(uint8_t press_type, uint32_t press_duration_ms) {
    sdf_event_router_event_t evt = {
        .type = SDF_EVENT_ROUTER_BUTTON_PRESS,
        .priority = SDF_EVENT_ROUTER_PRIO_NORMAL,
        .timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL),
        .payload.button = {.press_type = press_type, .press_duration_ms = press_duration_ms}
    };
    sdf_event_router_emit(&evt);
}

static void sdf_button_task_emit_admin_action_request(uint8_t action) {
    sdf_event_router_event_t evt = {
        .type = SDF_EVENT_ROUTER_ADMIN_ACTION_REQUEST,
        .priority = SDF_EVENT_ROUTER_PRIO_HIGH,
        .timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL),
        .payload.admin = {.action = action}
    };
    sdf_event_router_emit(&evt);
}

#ifndef CONFIG_IDF_TARGET_LINUX
static void IRAM_ATTR sdf_button_isr(void *arg) {
    (void)arg;
    BaseType_t higher_priority_task_woken = pdFALSE;
    xTaskNotifyFromISR(s_button_state.task_handle, 0, eNoAction, &higher_priority_task_woken);
    if (higher_priority_task_woken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}
#endif

/**
 * @brief Shared dispatch body for a resolved admin action, regardless of
 * whether the action was fixed at registration time (triple-click, holds)
 * or resolved dynamically at press time (single-click).
 *
 * The pending-admin-action gate below is unchanged from before this action
 * became state-dependent: NUKI_PAIR is only ever passed in here from the
 * single-click path when `sdf_services_get_setup_state()` returned
 * CLAIMED_INCOMPLETE, which by definition requires enrolled_user_count > 0
 * - so the "0 users, execute immediately" branch below can never be taken
 * for NUKI_PAIR, and it always goes through the admin-fingerprint pending
 * gate like every other non-enroll action.
 *
 * Not static: declared in sdf_services_internal.h so it can be driven
 * directly by host (linux target) unit tests without the real iot_button
 * GPIO plumbing - see test_sdf_services.c.
 */
void sdf_button_dispatch_action(sdf_services_admin_action_t action) {
    ESP_LOGI(TAG, "Button callback: action=%d", (int)action);

    sdf_services_state_t *s = sdf_services_state();

    if (xSemaphoreTake(s->lock, pdMS_TO_TICKS(SDF_SERVICES_LOCK_WAIT_MS)) != pdTRUE) {
        return;
    }

    /* If there are 0 users, there is no admin to authorize. Execute immediately. */
    if (s->enrolled_user_count == 0) {
        s->pending_admin_action = SDF_SERVICES_ADMIN_ACTION_NONE;
        s->pending_admin_action_start_us = 0;
        xSemaphoreGive(s->lock);

        /* ENROLL_ADMIN is no longer button-reachable (see the
         * button-admin-actions-to-companion-app change) so it can't land
         * here - only plain ENROLL (single-click on an unclaimed device)
         * still bypasses the admin-fingerprint gate below. */
        if (action == SDF_SERVICES_ADMIN_ACTION_ENROLL) {
            led_pulse_blue();
            sdf_services_request_enrollment(1, 3);
        } else {
            sdf_services_admin_action_cb action_cb = s->config.admin_action_cb;
            void *action_ctx = s->config.admin_action_ctx;
            if (action_cb != NULL) {
                action_cb(action_ctx, action);
            }
        }
        return;
    }

    /* Set the pending action and wait for Admin fingerprint */
    if (s->pending_admin_action == SDF_SERVICES_ADMIN_ACTION_NONE) {
        s->pending_admin_action = action;
        s->pending_admin_action_start_us = esp_timer_get_time();

        switch (action) {
            case SDF_SERVICES_ADMIN_ACTION_ENROLL:
            case SDF_SERVICES_ADMIN_ACTION_ENROLL_ADMIN:
                led_pulse_blue();
                break;
            case SDF_SERVICES_ADMIN_ACTION_NUKI_PAIR:
                led_pulse_yellow();
                break;
            case SDF_SERVICES_ADMIN_ACTION_ZB_JOIN:
                led_pulse_purple();
                break;
            case SDF_SERVICES_ADMIN_ACTION_FACTORY_RESET:
                led_pulse_red();
                break;
            case SDF_SERVICES_ADMIN_ACTION_BLE_PAIRING_WINDOW:
                /* Cyan mirrors NUKI_REPAIR's pending-action color in
                 * sdf_services_admin.c - both are BLE-Companion-Service-
                 * related admin actions. */
                led_pulse_cyan();
                break;
            default:
                break;
        }
        ESP_LOGI(TAG, "Hardware button pressed, pending admin action: %d", (int)action);
    }

    xSemaphoreGive(s->lock);
}

static void sdf_button_cb(void *arg, void *usr_data) {
    sdf_services_admin_action_t action = (sdf_services_admin_action_t)(uintptr_t)usr_data;
    sdf_button_dispatch_action(action);
}

/**
 * @brief Resolve single-click's action dynamically at press time based on
 * the device's current setup state, per the "State-Dependent Single-Click
 * Setup Action" requirement:
 *   - Unclaimed (no enrolled users)              -> ENROLL
 *   - Claimed, Nuki not yet paired                -> NUKI_PAIR
 *   - Claimed, Nuki already paired (setup complete) -> ENROLL
 */
static sdf_services_admin_action_t sdf_button_resolve_single_click_action(void) {
    sdf_services_setup_state_t setup_state = sdf_services_get_setup_state();

    if (setup_state == SDF_SERVICES_SETUP_STATE_CLAIMED_INCOMPLETE) {
        return SDF_SERVICES_ADMIN_ACTION_NUKI_PAIR;
    }
    return SDF_SERVICES_ADMIN_ACTION_ENROLL;
}

static void sdf_button_single_click_cb(void *arg, void *usr_data) {
    (void)arg;
    (void)usr_data;
    sdf_button_dispatch_action(sdf_button_resolve_single_click_action());
}

void sdf_button_task(void *arg) {
    (void)arg;
    sdf_services_state_t *s = sdf_services_state();

    sdf_button_task_init_subscriptions(&s_button_state);
    s_button_state.task_handle = xTaskGetCurrentTaskHandle();

#ifndef CONFIG_IDF_TARGET_LINUX
    /* Initialize button if GPIO configured */
    if (s->config.enrollment_btn_gpio >= 0) {
        button_config_t btn_config = {
            .long_press_time = 3000,
            .short_press_time = 180,
        };
        button_gpio_config_t gpio_config = {
            .gpio_num = s->config.enrollment_btn_gpio,
            .active_level = 0,
            .enable_power_save = false,
            .disable_pull = false,
        };

        if (iot_button_new_gpio_device(&btn_config, &gpio_config, &s_button_state.btn_handle) == ESP_OK) {
            iot_button_register_cb(s_button_state.btn_handle, BUTTON_SINGLE_CLICK, NULL,
                                   sdf_button_single_click_cb, NULL);

            /* Double-Press was retired as the Nuki-pairing trigger (see the
             * nuki-pairing-setup-flow change: single-click's action is now
             * state-dependent and reaches NUKI_PAIR itself once an admin
             * exists) and stayed free for future use until the
             * ble-companion-trust-and-lockout change picked it up as the
             * admin-fingerprint-gated trigger for the BLE Companion
             * Service's Device Pairing Window (see
             * sdf_ble_companion_open_pairing_window()). */
            iot_button_register_cb(s_button_state.btn_handle, BUTTON_DOUBLE_CLICK, NULL,
                                   sdf_button_cb,
                                   (void *)SDF_SERVICES_ADMIN_ACTION_BLE_PAIRING_WINDOW);

            /* Triple-click (ENROLL_ADMIN) and Hold-3s (ZB_JOIN) are
             * intentionally left unmapped as of the
             * button-admin-actions-to-companion-app change: both actions are
             * now only reachable via an authenticated companion-app request
             * (see sdf_app_on_ble_admin_action_request()), which still
             * requires the same admin-fingerprint authorization as before.
             * Both gestures remain free for future use. */

            button_event_args_t arg_8s = {.long_press = {.press_time = 8000}};
            iot_button_register_cb(s_button_state.btn_handle, BUTTON_LONG_PRESS_START, &arg_8s,
                                   sdf_button_cb, (void *)SDF_SERVICES_ADMIN_ACTION_FACTORY_RESET);

            ESP_LOGI(TAG, "Button initialized on GPIO %d", (int)s->config.enrollment_btn_gpio);
        } else {
            ESP_LOGW(TAG, "Failed to create enrollment iot_button");
        }
    }
#endif

    while (true) {
        {
            SDF_LOCK_GUARD(guard, s->lock, SDF_SERVICES_LOCK_WAIT_MS);
            if (guard.acquired == pdTRUE && s->stop_requested) {
                break;
            }
        }

        sdf_event_router_event_t event;
        TickType_t wait_ticks = pdMS_TO_TICKS(100);

        if (xQueueReceive(s_button_state.event_queue, &event, wait_ticks) == pdTRUE) {
            switch (event.type) {
                case SDF_EVENT_ROUTER_POWER_WAKE:
                    ESP_LOGI(TAG, "Wake event - resuming button task");
                    s_button_state.suspended = false;
                    break;
                case SDF_EVENT_ROUTER_POWER_SLEEP:
                    ESP_LOGI(TAG, "Sleep event - suspending button task");
                    s_button_state.suspended = true;
                    break;
                default:
                    break;
            }
        }

#ifndef CONFIG_IDF_TARGET_LINUX
        /* Process button notifications */
        uint32_t notify_val = 0;
        if (xTaskNotifyWait(0, 0, &notify_val, pdMS_TO_TICKS(10)) == pdTRUE) {
            /* Button ISR notified us - button library handles callbacks */
        }
#endif
    }

    /* Cooperative shutdown requested via sdf_services_stop_tasks(): unwind
     * cleanly instead of being killed from outside. */
    sdf_button_task_deinit_subscriptions(&s_button_state);
#ifndef CONFIG_IDF_TARGET_LINUX
    if (s_button_state.btn_handle != NULL) {
        iot_button_delete(s_button_state.btn_handle);
        s_button_state.btn_handle = NULL;
    }
#endif
    {
        SDF_LOCK_GUARD(guard, s->lock, SDF_SERVICES_LOCK_WAIT_MS);
        if (guard.acquired == pdTRUE) {
            s->button_task = NULL;
        }
    }
    vTaskDelete(NULL);
}