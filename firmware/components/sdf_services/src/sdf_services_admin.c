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

#include "sdf_event_router.h"

#define SDF_ADMIN_TASK_NAME "sdf_admin"
#define SDF_ADMIN_TASK_STACK 4096
#define SDF_ADMIN_TASK_PRIORITY 6
#define SDF_ADMIN_ACTION_TIMEOUT_MS 10000

static const char *TAG = "sdf_services_admin";

typedef struct {
    QueueHandle_t event_queue;
    sdf_event_router_subscriber_t *sub_action_req;
    sdf_event_router_subscriber_t *sub_match;
    sdf_event_router_subscriber_t *sub_power_wake;
    sdf_event_router_subscriber_t *sub_power_sleep;
    TaskHandle_t task_handle;
    bool suspended;
} sdf_admin_task_state_t;

static sdf_admin_task_state_t s_admin_state = {0};

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

    sdf_event_router_subscribe(SDF_EVENT_ROUTER_BIOMETRIC_MATCH,
                               SDF_EVENT_ROUTER_PRIO_HIGH,
                               sdf_admin_task_event_cb, state, &state->sub_match);

    sdf_event_router_subscribe(SDF_EVENT_ROUTER_POWER_WAKE,
                               SDF_EVENT_ROUTER_PRIO_CRITICAL,
                               sdf_admin_task_event_cb, state, &state->sub_power_wake);

    sdf_event_router_subscribe(SDF_EVENT_ROUTER_POWER_SLEEP,
                               SDF_EVENT_ROUTER_PRIO_CRITICAL,
                               sdf_admin_task_event_cb, state, &state->sub_power_sleep);
}

static void sdf_admin_task_deinit_subscriptions(sdf_admin_task_state_t *state) {
    if (state->sub_action_req) sdf_event_router_unsubscribe(state->sub_action_req);
    if (state->sub_match) sdf_event_router_unsubscribe(state->sub_match);
    if (state->sub_power_wake) sdf_event_router_unsubscribe(state->sub_power_wake);
    if (state->sub_power_sleep) sdf_event_router_unsubscribe(state->sub_power_sleep);
    if (state->event_queue) vQueueDelete(state->event_queue);
    state->event_queue = NULL;
}

static void sdf_admin_task_emit_auth_result(uint16_t user_id, uint8_t permission,
                                             bool authorized) {
    sdf_event_router_event_t evt = {
        .type = SDF_EVENT_ROUTER_ADMIN_AUTH_RESULT,
        .priority = SDF_EVENT_ROUTER_PRIO_HIGH,
        .timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL),
        .payload.admin_auth = {.user_id = user_id,
                               .permission = permission,
                               .authorized = authorized}
    };
    sdf_event_router_emit(&evt);
}

static void sdf_admin_task_emit_action_complete(sdf_services_admin_action_t action,
                                                 int8_t result) {
    sdf_event_router_event_t evt = {
        .type = SDF_EVENT_ROUTER_ADMIN_ACTION_COMPLETE,
        .priority = SDF_EVENT_ROUTER_PRIO_HIGH,
        .timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL),
        .payload.admin_action_complete = {.action = action, .result = result}
    };
    sdf_event_router_emit(&evt);
}

void sdf_admin_task(void *arg) {
    (void)arg;
    sdf_services_state_t *s = sdf_services_state();

    sdf_admin_task_init_subscriptions(&s_admin_state);
    s_admin_state.task_handle = xTaskGetCurrentTaskHandle();

    while (true) {
        sdf_event_router_event_t event;
        if (xQueueReceive(s_admin_state.event_queue, &event, pdMS_TO_TICKS(100)) == pdTRUE) {
            switch (event.type) {
                case SDF_EVENT_ROUTER_ADMIN_ACTION_REQUEST: {
                    ESP_LOGI(TAG, "Admin action request: %u", (unsigned)event.payload.admin.action);

                    if (xSemaphoreTake(s->lock, pdMS_TO_TICKS(SDF_SERVICES_LOCK_WAIT_MS)) == pdTRUE) {
                        if (s->pending_admin_action == SDF_SERVICES_ADMIN_ACTION_NONE) {
                            s->pending_admin_action = (sdf_services_admin_action_t)event.payload.admin.action;
                            s->pending_admin_action_start_us = esp_timer_get_time();

                            /* LED indication based on action type */
                            switch (s->pending_admin_action) {
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
                                default:
                                    break;
                            }
                            ESP_LOGI(TAG, "Pending admin action set: %d", (int)s->pending_admin_action);
                        }
                        xSemaphoreGive(s->lock);
                    }
                    break;
                }

                case SDF_EVENT_ROUTER_BIOMETRIC_MATCH: {
                    /* Check if we have a pending admin action */
                    sdf_services_admin_action_t action = SDF_SERVICES_ADMIN_ACTION_NONE;
                    sdf_services_admin_action_cb action_cb = NULL;
                    void *action_ctx = NULL;
                    bool authorized = false;

                    if (xSemaphoreTake(s->lock, pdMS_TO_TICKS(SDF_SERVICES_LOCK_WAIT_MS)) == pdTRUE) {
                        if (s->pending_admin_action != SDF_SERVICES_ADMIN_ACTION_NONE) {
                            if (event.payload.biometric.user_id > 0 && event.payload.biometric.confidence > 0) {
                                /* The biometric event doesn't have permission, we'd need to check it */
                                /* For now, assume admin permission if user_id > 0 */
                                authorized = true;
                                action = s->pending_admin_action;
                                action_cb = s->config.admin_action_cb;
                                action_ctx = s->config.admin_action_ctx;
                                s->pending_admin_action = SDF_SERVICES_ADMIN_ACTION_NONE;
                                s->pending_admin_action_start_us = 0;
                            } else {
                                authorized = false;
                            }
                        }
                        xSemaphoreGive(s->lock);
                    }

                    if (s->pending_admin_action != SDF_SERVICES_ADMIN_ACTION_NONE) {
                        sdf_admin_task_emit_auth_result(event.payload.biometric.user_id,
                                                         event.payload.biometric.confidence,
                                                         authorized);

                        if (authorized) {
                            ESP_LOGI(TAG, "Admin auth success for action %d", (int)action);
                            led_admin_auth_green();
                            sdf_services_execute_admin_action(action, action_cb, action_ctx);
                            sdf_admin_task_emit_action_complete(action, ESP_OK);
                        } else {
                            ESP_LOGW(TAG, "Admin auth rejected: non-admin user");
                            led_admin_auth_red();
                            sdf_admin_task_emit_action_complete(action, ESP_ERR_NOT_SUPPORTED);
                        }
                    }
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

                default:
                    break;
            }
        }

        /* Check for admin action timeout */
        if (xSemaphoreTake(s->lock, pdMS_TO_TICKS(SDF_SERVICES_LOCK_WAIT_MS)) == pdTRUE) {
            if (s->pending_admin_action != SDF_SERVICES_ADMIN_ACTION_NONE &&
                s->pending_admin_action_start_us > 0) {
                int64_t now_us = esp_timer_get_time();
                if ((now_us - s->pending_admin_action_start_us) > ((int64_t)SDF_ADMIN_ACTION_TIMEOUT_MS * 1000LL)) {
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
}