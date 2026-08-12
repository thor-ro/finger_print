#include "sdf_services_internal.h"
#include "sdf_drivers.h"
#include "sdf_platform.h"
#include "sdf_config.h"
#include "sdf_event_router.h"
#include "sdf_app.h"
#include "sdf_storage.h"

#include "esp_system.h"

#include <stdlib.h>
#include <string.h>

#ifdef CONFIG_IDF_TARGET_LINUX
#include "sdf_mock_linux_gpio.h"
#else
#include "button_gpio.h"
#include "iot_button.h"
#endif

#include "esp_log.h"
#include "esp_timer.h"
#ifndef CONFIG_IDF_TARGET_LINUX
#include "esp_task_wdt.h"
#endif
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"

/* Task configuration constants */
#define SDF_MATCH_TASK_NAME "sdf_match"
#define SDF_MATCH_TASK_STACK 4096
#define SDF_MATCH_TASK_PRIORITY 5

#define SDF_ENROLL_TASK_NAME "sdf_enroll"
#define SDF_ENROLL_TASK_STACK 4096
#define SDF_ENROLL_TASK_PRIORITY 4

#define SDF_ADMIN_TASK_NAME "sdf_admin"
#define SDF_ADMIN_TASK_STACK 4096
#define SDF_ADMIN_TASK_PRIORITY 5

#define SDF_BUTTON_TASK_NAME "sdf_btn"
#define SDF_BUTTON_TASK_STACK 3072
#define SDF_BUTTON_TASK_PRIORITY 4

#define SDF_SERVICES_DEFAULT_UART_PORT 1
#define SDF_SERVICES_DEFAULT_UART_TX_PIN 0
#define SDF_SERVICES_DEFAULT_UART_RX_PIN 1
#define SDF_SERVICES_DEFAULT_UART_BAUD_RATE 19200u
#define SDF_SERVICES_DEFAULT_UART_TIMEOUT_MS 12000u
#define SDF_SERVICES_DEFAULT_UART_RX_BUFFER 256u
#define SDF_SERVICES_DEFAULT_UART_TX_BUFFER 256u

#define SDF_SERVICES_DEFAULT_MATCH_POLL_MS 400u
#define SDF_SERVICES_DEFAULT_MATCH_COOLDOWN_MS 3000u
#define SDF_SERVICES_DEFAULT_FAILED_ATTEMPT_THRESHOLD                          \
  ((uint32_t)CONFIG_SDF_SECURITY_BIOMETRIC_FAIL_THRESHOLD)
#define SDF_SERVICES_DEFAULT_FAILED_ATTEMPT_WINDOW_MS                          \
  ((uint32_t)CONFIG_SDF_SECURITY_BIOMETRIC_FAIL_WINDOW_MS)
#define SDF_SERVICES_DEFAULT_LOCKOUT_DURATION_MS                               \
  ((uint32_t)CONFIG_SDF_SECURITY_BIOMETRIC_LOCKOUT_MS)
#define SDF_SERVICES_ADMIN_ACTION_TIMEOUT_MS 10000u
#define SDF_SERVICES_PERMISSION_CHANGE_WAIT_MS 15000u

/* Maximum users supported by firmware. Aliased to the fingerprint driver's
 * own limit (fingerprint.h) rather than a separate hardcoded literal, so
 * the two can't silently drift apart. The sensor itself supports far more
 * (up to 4095); this cap is a firmware-side RAM/UX choice. */
#define SDF_SERVICES_MAX_USERS SDF_FINGERPRINT_USER_ID_MAX
/* Packed permissions: 2 bits per user, 8 users per uint8_t */
#define SDF_SERVICES_PERM_PACKED_SIZE 4u  /* 16 users * 2 bits = 32 bits = 4 bytes */

static const char *TAG = "sdf_services";

/* Compact user representation: bitmap (1 bit per user ID) + packed 2-bit permissions.
 * Max 10 users -> 16-bit bitmap + 4-byte packed permissions (16 users * 2 bits).
 * Replaces 4x512 buffers (3072 bytes) with ~12 bytes.
 * Optimization #16: ~50%+ RAM savings with bitmap + packed perms.
 *
 * Bitmap: bit N = 1 means user ID (N+1) is enrolled
 * Packed perms: 2 bits per user (0=none, 1=std, 2=elev, 3=admin) */
static uint16_t s_enrollment_user_bmp = 0;
static uint8_t s_enrollment_perm_packed[SDF_SERVICES_PERM_PACKED_SIZE] = {0};
static uint16_t s_perm_user_bmp = 0;
static uint8_t s_perm_perm_packed[SDF_SERVICES_PERM_PACKED_SIZE] = {0};

/* Convert sensor query results (user_ids[], permissions[]) to packed format */
void sdf_services_pack_user_list(const uint16_t *user_ids,
                                        const uint8_t *permissions,
                                        size_t count,
                                        uint16_t *bmp,
                                        uint8_t *perm_packed)
{
    *bmp = 0;
    memset(perm_packed, 0, SDF_SERVICES_PERM_PACKED_SIZE);
    for (size_t i = 0; i < count; i++) {
        /* user_ids[] comes straight from the sensor's query response, not
         * from data we control. An out-of-range entry (0, or beyond the
         * bitmap/packed-permissions capacity) must be skipped, not treated
         * as "stop processing the rest of the list" - a `&&` loop condition
         * here would silently drop every user after the first bad one.
         * id 0 specifically also can't be passed to the SDF_SERVICES_BMP_SET
         * macro or sdf_services_perm_set() below: they compute (id - 1) as a
         * bit/array index, and id=0 makes that wrap to a huge unsigned value
         * (or, via the intermediate signed subtraction, a negative
         * shift/index) - undefined behavior. */
        if (user_ids[i] < SDF_FINGERPRINT_USER_ID_MIN ||
            user_ids[i] > SDF_SERVICES_MAX_USERS) {
            ESP_LOGW(TAG, "Skipping out-of-range user_id=%u from sensor query",
                     (unsigned)user_ids[i]);
            continue;
        }
        SDF_SERVICES_BMP_SET(*bmp, user_ids[i]);
        sdf_services_perm_set(perm_packed, user_ids[i], permissions[i]);
    }
}

static sdf_services_state_t s_state = {0};

bool sdf_services_try_claim_admin_action(
    const sdf_fingerprint_match_t *match);

sdf_services_state_t *sdf_services_state(void) {
  return &s_state;
}

const char *sdf_services_fingerprint_result_name(
    sdf_fingerprint_op_result_t result) {
  switch (result) {
  case SDF_FINGERPRINT_OP_OK:
    return "OK";
  case SDF_FINGERPRINT_OP_NO_MATCH:
    return "NO_MATCH";
  case SDF_FINGERPRINT_OP_TIMEOUT:
    return "TIMEOUT";
  case SDF_FINGERPRINT_OP_FULL:
    return "FULL";
  case SDF_FINGERPRINT_OP_USER_OCCUPIED:
    return "USER_OCCUPIED";
  case SDF_FINGERPRINT_OP_FINGER_OCCUPIED:
    return "FINGER_OCCUPIED";
  case SDF_FINGERPRINT_OP_FAILED:
    return "FAILED";
  case SDF_FINGERPRINT_OP_IO_ERROR:
    return "IO_ERROR";
  case SDF_FINGERPRINT_OP_PROTOCOL_ERROR:
    return "PROTOCOL_ERROR";
  case SDF_FINGERPRINT_OP_BAD_ARG:
    return "BAD_ARG";
  default:
    return "UNKNOWN";
  }
}

esp_err_t sdf_services_fingerprint_result_to_err(
    sdf_fingerprint_op_result_t result) {
  switch (result) {
  case SDF_FINGERPRINT_OP_OK:
    return ESP_OK;
  case SDF_FINGERPRINT_OP_TIMEOUT:
    return ESP_ERR_TIMEOUT;
  case SDF_FINGERPRINT_OP_BAD_ARG:
    return ESP_ERR_INVALID_ARG;
  case SDF_FINGERPRINT_OP_PROTOCOL_ERROR:
    return ESP_ERR_INVALID_RESPONSE;
  default:
    return ESP_FAIL;
  }
}

void sdf_services_complete_permission_change(esp_err_t result) {
  SemaphoreHandle_t done_sem = NULL;
  bool should_signal = false;

  if (s_state.lock == NULL) {
    return;
  }

  {
    SDF_LOCK_GUARD(guard, s_state.lock, SDF_SERVICES_LOCK_WAIT_MS);
    if (guard.acquired != pdTRUE) {
      return;
    }

    if (!s_state.permission_change_pending) {
      return;
    }

    s_state.permission_change_result = result;
    s_state.permission_change_pending = false;
    s_state.permission_change_user_id = 0;
    s_state.permission_change_permission = 0;
    done_sem = s_state.admin_action_done_sem;
    should_signal = (done_sem != NULL);
  }

  if (should_signal) {
    xSemaphoreGive(done_sem);
  }
}

static void
sdf_services_emit_enrollment_event(sdf_event_router_type_t type,
                                   const sdf_enrollment_sm_t *sm) {
  if (!sdf_services_is_ready()) {
    return;
  }

  sdf_event_router_event_t evt = {
      .type = type,
      .timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL),
      .priority = SDF_EVENT_ROUTER_PRIO_NORMAL,
      .payload.enrollment.step = sm->completed_steps,
      .payload.enrollment.status = sm->state,
  };
  sdf_event_router_emit(&evt);
}

static void
sdf_services_start_local_enrollment_with_permission(
    uint8_t permission) {
  const size_t max_users = (size_t)SDF_SERVICES_MAX_USERS;
  size_t count = 0;
  uint16_t new_id = 0;
  esp_err_t err = ESP_OK;

  if (esp_get_free_heap_size() < 4096) {
    ESP_LOGE(TAG, "Insufficient heap for enrollment query");
    sdf_app_emit_audit(SDF_AUDIT_PROTOCOL_ERROR, 0, ESP_ERR_NO_MEM, 0);
    led_flash_red();
    return;
  }

  /* Use local temp arrays for query, then pack into compact format */
  uint16_t user_ids[SDF_SERVICES_MAX_USERS];
  uint8_t perms[SDF_SERVICES_MAX_USERS];

  err = sdf_services_query_users(user_ids, perms, &count, max_users);
  // query_users involves a sensor UART exchange that can be slow, but the
  // fingerprint owner task now resets its own watchdog entry per dispatched
  // request, and this task resets its own entry while blocked waiting for
  // the reply - no per-call-site reset needed here even though this runs
  // right after fp_match_1n() inside sdf_services_execute_admin_action.
  if (err == ESP_OK) {
    /* Pack results into compact bitmap + packed permissions format */
    sdf_services_pack_user_list(user_ids, perms, count,
                                &s_enrollment_user_bmp,
                                s_enrollment_perm_packed);

    /* Find first free ID using bitmap (O(1) per check) */
    for (uint16_t id = 1; id <= SDF_SERVICES_MAX_USERS; id++) {
      if (!SDF_SERVICES_BMP_TEST(s_enrollment_user_bmp, id)) {
        new_id = id;
        break;
      }
    }
  } else {
    ESP_LOGW(TAG, "Failed to query users for local enrollment: %s",
             esp_err_to_name(err));
  }

  if (new_id == 0) {
    if (err == ESP_OK) {
      ESP_LOGW(TAG, "No free user IDs available for local enrollment");
    }
    led_flash_red();
    return;
  }

  err = sdf_services_request_enrollment(new_id, permission);
  if (err != ESP_OK) {
    ESP_LOGW(TAG,
             "Failed to queue local enrollment for user_id=%u permission=%u: "
             "%s",
             (unsigned)new_id, (unsigned)permission, esp_err_to_name(err));
    led_flash_red();
  }
}

void sdf_services_execute_admin_action(
    sdf_services_admin_action_t action,
    sdf_services_admin_action_cb action_cb, void *action_ctx) {
  ESP_LOGI(TAG, "Authorized action %d!", (int)action);

  if (action == SDF_SERVICES_ADMIN_ACTION_ENROLL) {
    sdf_services_start_local_enrollment_with_permission(1u);
    return;
  }

  if (action == SDF_SERVICES_ADMIN_ACTION_ENROLL_ADMIN) {
    sdf_services_start_local_enrollment_with_permission(3u);
    /* Unlike plain ENROLL (button-only, nothing to notify), ENROLL_ADMIN can
     * also be requested over BLE, so action_cb still needs to run afterwards
     * purely so sdf_app can route a reply back to the requesting connection
     * - the enrollment side effect above is unchanged either way. */
    if (action_cb != NULL) {
      action_cb(action_ctx, action);
    }
    return;
  }

  if (action == SDF_SERVICES_ADMIN_ACTION_CHANGE_PERMISSION) {
    uint16_t user_id = 0;
    uint8_t permission = 0;
    {
      SDF_LOCK_GUARD(guard, s_state.lock, SDF_SERVICES_LOCK_WAIT_MS);
      if (guard.acquired != pdTRUE) {
        sdf_services_complete_permission_change(ESP_ERR_TIMEOUT);
        led_flash_red();
        return;
      }

      user_id = s_state.permission_change_user_id;
      permission = s_state.permission_change_permission;
    }

    sdf_fingerprint_op_result_t fp_result =
        fp_change_user_permission(user_id, permission);
    esp_err_t err = sdf_services_fingerprint_result_to_err(fp_result);

    if (err == ESP_OK) {
      ESP_LOGI(TAG, "Changed fingerprint permission for user_id=%u to %u",
               (unsigned)user_id, (unsigned)permission);
      led_flash_green();
    } else {
      ESP_LOGW(TAG,
               "Failed to change fingerprint permission for user_id=%u to %u: "
               "%s",
               (unsigned)user_id, (unsigned)permission,
               sdf_services_fingerprint_result_name(fp_result));
      led_flash_red();
    }

    sdf_services_complete_permission_change(err);
    return;
  }

  if (action == SDF_SERVICES_ADMIN_ACTION_WEB_REG_AUTH) {
    ESP_LOGI(TAG, "Admin authorized Web Registration");
    sdf_event_router_event_t evt = {
        .type = SDF_EVENT_ROUTER_WEB_REG_AUTH_RESULT,
        .priority = SDF_EVENT_ROUTER_PRIO_HIGH,
        .timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL),
    };
    sdf_services_state_t *s = sdf_services_state();
    if (xSemaphoreTake(s->lock, pdMS_TO_TICKS(SDF_SERVICES_LOCK_WAIT_MS)) == pdTRUE) {
        strncpy(evt.payload.web_reg_auth_result.username, s->request_web_username,
                SDF_STORAGE_WEB_USER_NAME_MAX - 1);
        evt.payload.web_reg_auth_result.username[SDF_STORAGE_WEB_USER_NAME_MAX - 1] = '\0';
        evt.payload.web_reg_auth_result.authorized = true;
        evt.payload.web_reg_auth_result.permission = s->request_web_permission;
        xSemaphoreGive(s->lock);
    }
    sdf_event_router_emit(&evt);
    return;
  }

  if (action_cb != NULL) {
    action_cb(action_ctx, action);
  }
}

static void IRAM_ATTR sdf_services_wake_isr(void *arg) {
  (void)arg;
  BaseType_t higher_priority_task_woken = pdFALSE;
  xSemaphoreGiveFromISR(s_state.wake_sem, &higher_priority_task_woken);
  if (s_state.match_task_queue != NULL) {
    sdf_event_router_event_t evt = {
        .type = SDF_EVENT_ROUTER_BIOMETRIC_MATCH_REQUEST,
        .timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL),
        .priority = SDF_EVENT_ROUTER_PRIO_HIGH,
    };
    xQueueSendFromISR(s_state.match_task_queue, &evt, &higher_priority_task_woken);
  }
  if (higher_priority_task_woken == pdTRUE) {
#ifdef CONFIG_IDF_TARGET_LINUX
    portYIELD_FROM_ISR(pdTRUE);
#else
    portYIELD_FROM_ISR();
#endif
  }
}

/**
 * @brief Attempt to claim and execute a pending admin action using a
 *        fingerprint match result.
 *
 * Checks whether there is a pending admin action and whether the matched
 * fingerprint has admin privileges (permission == 3).  If both conditions
 * are met, the pending action is claimed (cleared from state) and executed.
 *
 * @param match  The fingerprint match result to authorize with.
 * @return true  if a pending action was present (regardless of whether it
 *               was authorized — the caller should NOT proceed with a
 *               normal unlock).
 * @return false if no pending admin action exists (caller should proceed
 *               with normal unlock flow).
 */
bool sdf_services_try_claim_admin_action(
    const sdf_fingerprint_match_t *match) {
  sdf_services_admin_action_t action = SDF_SERVICES_ADMIN_ACTION_NONE;
  sdf_services_admin_action_cb action_cb = NULL;
  void *action_ctx = NULL;
  bool has_pending = false;
  bool authorized = false;

  if (xSemaphoreTake(s_state.lock,
                     pdMS_TO_TICKS(SDF_SERVICES_LOCK_WAIT_MS)) == pdTRUE) {
    has_pending =
        (s_state.pending_admin_action != SDF_SERVICES_ADMIN_ACTION_NONE);
    if (has_pending && match->permission == 3) {
      action = s_state.pending_admin_action;
      action_cb = s_state.config.admin_action_cb;
      action_ctx = s_state.config.admin_action_ctx;
      s_state.pending_admin_action = SDF_SERVICES_ADMIN_ACTION_NONE;
      s_state.pending_admin_action_start_us = 0;
      authorized = true;
    }
    xSemaphoreGive(s_state.lock);
  }

  if (!has_pending) {
    return false;
  }

  if (authorized) {
    ESP_LOGI(TAG,
             "Pending admin action %d consumed by user_id=%u permission=%u",
             (int)action, (unsigned)match->user_id,
             (unsigned)match->permission);
    led_admin_auth_green();
    sdf_services_execute_admin_action(action, action_cb, action_ctx);
  } else {
    ESP_LOGW(TAG,
             "Admin auth rejected: user_id=%u permission=%u != ADMIN(3)",
             (unsigned)match->user_id, (unsigned)match->permission);
    led_admin_auth_red();
  }

  return true;
}

/* Legacy task removed - boot init now handled by sdf_match_task, enrollment by sdf_enroll_task */

void sdf_services_get_default_config(sdf_services_config_t *config) {
  const sdf_config_t *sdf_cfg = sdf_config_get();
  if (config == NULL) {
    return;
  }

  memset(config, 0, sizeof(*config));
  config->fingerprint.uart_port = sdf_cfg->fp_uart_port;
  config->fingerprint.tx_pin = sdf_cfg->fp_tx_pin;
  config->fingerprint.rx_pin = sdf_cfg->fp_rx_pin;
  config->fingerprint.baud_rate = sdf_cfg->fp_baud_rate;
  config->fingerprint.response_timeout_ms = sdf_cfg->fp_response_timeout_ms;
  config->fingerprint.rx_buffer_size = sdf_cfg->fp_rx_buffer_size;
  config->fingerprint.tx_buffer_size = sdf_cfg->fp_tx_buffer_size;

  config->match_poll_interval_ms = sdf_cfg->match_poll_interval_ms;
  config->match_cooldown_ms = sdf_cfg->match_cooldown_ms;
  config->failed_attempt_threshold = sdf_cfg->failed_attempt_threshold;
  config->failed_attempt_window_ms = sdf_cfg->failed_attempt_window_ms;
  config->lockout_duration_ms = sdf_cfg->lockout_duration_ms;
  config->wake_gpio = (gpio_num_t)sdf_cfg->fp_wake_gpio;
  config->power_en_gpio = (gpio_num_t)sdf_cfg->fp_en_gpio;
  config->enrollment_btn_gpio = (gpio_num_t)sdf_cfg->enrollment_btn_gpio;
  config->ws2812_led_gpio = (gpio_num_t)sdf_cfg->ws2812_led_gpio;
  config->battery_adc_pin = sdf_cfg->battery_adc_pin;

}

/* New task start/stop functions */
esp_err_t sdf_services_start_tasks(void) {
    sdf_services_state_t *s = &s_state;

    if (!sdf_services_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Create match task queue for ISR to wake the task. sdf_services_init()
     * already creates this queue, so only create it here if it's somehow
     * missing (e.g. this is called outside the normal init path) -
     * unconditionally recreating it would leak the previous queue handle. */
    if (s->match_task_queue == NULL) {
        s->match_task_queue = xQueueCreate(10, sizeof(sdf_event_router_event_t));
        if (s->match_task_queue == NULL) {
            ESP_LOGE(TAG, "Failed to create match task queue");
            return ESP_FAIL;
        }
    }

    /* Create match task */
    BaseType_t task_ok = xTaskCreate(sdf_match_task, SDF_MATCH_TASK_NAME,
                                     SDF_MATCH_TASK_STACK, NULL,
                                     SDF_MATCH_TASK_PRIORITY, &s->match_task);
    if (task_ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create match task");
        return ESP_FAIL;
    }

    /* Create enrollment task */
    task_ok = xTaskCreate(sdf_enroll_task, SDF_ENROLL_TASK_NAME,
                          SDF_ENROLL_TASK_STACK, NULL,
                          SDF_ENROLL_TASK_PRIORITY, &s->enroll_task);
    if (task_ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create enroll task");
        vTaskDelete(s->match_task);
        s->match_task = NULL;
        return ESP_FAIL;
    }

    /* Create admin task */
    task_ok = xTaskCreate(sdf_admin_task, SDF_ADMIN_TASK_NAME,
                          SDF_ADMIN_TASK_STACK, NULL,
                          SDF_ADMIN_TASK_PRIORITY, &s->admin_task);
    if (task_ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create admin task");
        vTaskDelete(s->match_task);
        vTaskDelete(s->enroll_task);
        s->match_task = NULL;
        s->enroll_task = NULL;
        return ESP_FAIL;
    }

    /* Create button task */
    task_ok = xTaskCreate(sdf_button_task, SDF_BUTTON_TASK_NAME,
                          SDF_BUTTON_TASK_STACK, NULL,
                          SDF_BUTTON_TASK_PRIORITY, &s->button_task);
    if (task_ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create button task");
        vTaskDelete(s->match_task);
        vTaskDelete(s->enroll_task);
        vTaskDelete(s->admin_task);
        s->match_task = NULL;
        s->enroll_task = NULL;
        s->admin_task = NULL;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "All 4 services tasks started");
    return ESP_OK;
}

esp_err_t sdf_services_stop_tasks(void) {
    sdf_services_state_t *s = &s_state;

    /* Stop the wake GPIO ISR first so it can't push a new event into
     * match_task_queue (sdf_services_wake_isr) once the tasks/queue below
     * are torn down - that would be a use-after-free of the queue handle. */
    if (s->config.wake_gpio >= 0) {
        sdf_platform_gpio_isr_handler_remove(s->config.wake_gpio);
    }

    if (s->lock != NULL) {
        SDF_LOCK_GUARD(guard, s->lock, SDF_SERVICES_LOCK_WAIT_MS);
        if (guard.acquired == pdTRUE) {
            s->stop_requested = true;
        }
    }

    /* Each task polls stop_requested (see sdf_match_task / sdf_enroll_task /
     * sdf_admin_task / sdf_button_task) and exits on its own -
     * unsubscribing from the event router and self-deleting - instead of
     * being killed from outside via vTaskDelete(). Killing a task from
     * outside can leave s_state.lock permanently held if the victim
     * happened to be inside a critical section (or, for match/enroll, mid
     * the ~12s blocking fingerprint UART call) at the moment of deletion.
     * Poll for all four to clear their handles, bounded generously enough
     * to cover that worst-case 12s UART timeout. */
    const int poll_interval_ms = 50;
    const int max_wait_ms = 13000;
    int waited_ms = 0;
    bool all_stopped = false;
    while (waited_ms < max_wait_ms) {
        {
            /* Scoped so the lock is released before vTaskDelay() below -
             * holding it across the delay would needlessly block every
             * other s_state.lock user for the whole polling window. */
            SDF_LOCK_GUARD(guard, s->lock, SDF_SERVICES_LOCK_WAIT_MS);
            all_stopped = (guard.acquired == pdTRUE) && s->match_task == NULL &&
                          s->enroll_task == NULL && s->admin_task == NULL &&
                          s->button_task == NULL;
        }
        if (all_stopped) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(poll_interval_ms));
        waited_ms += poll_interval_ms;
    }

    if (!all_stopped) {
        ESP_LOGE(TAG,
                 "One or more services tasks did not stop within %dms; "
                 "forcing deletion (may leave s_state.lock permanently held "
                 "if a task was mid-critical-section)",
                 max_wait_ms);
        if (s->match_task) {
            vTaskDelete(s->match_task);
            s->match_task = NULL;
        }
        if (s->enroll_task) {
            vTaskDelete(s->enroll_task);
            s->enroll_task = NULL;
        }
        if (s->admin_task) {
            vTaskDelete(s->admin_task);
            s->admin_task = NULL;
        }
        if (s->button_task) {
            vTaskDelete(s->button_task);
            s->button_task = NULL;
        }
    }

    if (s->match_task_queue) {
        vQueueDelete(s->match_task_queue);
        s->match_task_queue = NULL;
    }

    if (s->lock != NULL) {
        SDF_LOCK_GUARD(guard, s->lock, SDF_SERVICES_LOCK_WAIT_MS);
        if (guard.acquired == pdTRUE) {
            s->stop_requested = false;
        }
    }

    ESP_LOGI(TAG, "All services tasks stopped");
    return ESP_OK;
}


esp_err_t sdf_services_init(const sdf_services_config_t *config) {
  if (config == NULL || config->match_poll_interval_ms == 0 ||
      config->match_cooldown_ms == 0 || config->failed_attempt_threshold == 0 ||
      config->failed_attempt_window_ms == 0 ||
      config->lockout_duration_ms == 0) {
    return ESP_ERR_INVALID_ARG;
  }

  if (s_state.lock == NULL) {
    s_state.lock = xSemaphoreCreateMutex();
    s_state.wake_sem = xSemaphoreCreateBinary();
    s_state.admin_action_done_sem = xSemaphoreCreateBinary();
    s_state.match_task_queue = xQueueCreate(10, sizeof(sdf_event_router_event_t));
    if (s_state.lock == NULL || s_state.wake_sem == NULL ||
        s_state.admin_action_done_sem == NULL || s_state.match_task_queue == NULL) {
      return ESP_ERR_NO_MEM;
    }
  }

  if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(SDF_SERVICES_LOCK_WAIT_MS)) !=
      pdTRUE) {
    return ESP_ERR_TIMEOUT;
  }

  if (s_state.initialized) {
    xSemaphoreGive(s_state.lock);
    return ESP_OK;
  }

  s_state.config = *config;
  s_state.config.fingerprint.power_en_pin = config->power_en_gpio;
  sdf_enrollment_sm_init(&s_state.enrollment);
  s_state.enrollment_request_pending = false;
  s_state.request_user_id = 0;
  s_state.request_permission = 0;
  s_state.match_cooldown_until_us = 0;
  s_state.failed_attempt_count = 0;
  s_state.failed_attempt_window_start_us = 0;
  s_state.lockout_until_us = 0;
  s_state.pending_admin_action = SDF_SERVICES_ADMIN_ACTION_NONE;
  s_state.pending_admin_action_start_us = 0;
  s_state.permission_change_pending = false;
  s_state.permission_change_user_id = 0;
  s_state.permission_change_permission = 0;
  s_state.permission_change_result = ESP_OK;
  s_state.enrolled_user_count = 0;
  xSemaphoreGive(s_state.lock);

sdf_drivers_config_t drivers_config = {
       .fingerprint = s_state.config.fingerprint,
       .led = {
           .gpio_num = config->ws2812_led_gpio,
       },
       .battery_adc_pin = config->battery_adc_pin,
       .fp_wake_gpio = config->wake_gpio,
   };

  esp_err_t err = sdf_drivers_init(&drivers_config);
  if (err != ESP_OK) {
    return err;
  }

  /* Start new tasks instead of legacy single task */
  err = sdf_services_start_tasks();
  if (err != ESP_OK) {
    sdf_drivers_deinit();
    return err;
  }

  if (config->wake_gpio >= 0) {
    err = sdf_platform_gpio_configure_wake(config->wake_gpio,
                                           GPIO_INTR_ANYEDGE,
                                           false,
                                           true,
                                           sdf_services_wake_isr,
                                           NULL);
    if (err != ESP_OK) {
      ESP_LOGW(TAG, "Failed to configure wake GPIO interrupt: %s",
               esp_err_to_name(err));
    }
  }

  /* Button is now handled by sdf_button_task */

  if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(SDF_SERVICES_LOCK_WAIT_MS)) ==
      pdTRUE) {
    s_state.initialized = true;
    xSemaphoreGive(s_state.lock);
  }

  ESP_LOGI(TAG, "Fingerprint services initialized");
  return ESP_OK;
}

bool sdf_services_is_ready(void) {
  if (s_state.lock == NULL) {
    return false;
  }

  bool ready = false;
  if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(SDF_SERVICES_LOCK_WAIT_MS)) ==
      pdTRUE) {
    ready = s_state.initialized;
    xSemaphoreGive(s_state.lock);
  }
  return ready;
}

void sdf_services_trigger_low_battery_warning(void) {
  led_flash_orange();
}

esp_err_t sdf_services_delete_user(uint16_t user_id) {
  if (s_state.lock == NULL)
    return ESP_ERR_INVALID_STATE;
  /* fp_delete_user() is a blocking UART round-trip (up to the ~12s sensor
   * timeout) and is already serialized by the fingerprint driver's own
   * internal mutex, so s_state.lock does not need to be held across it -
   * doing so would stall every other s_state.lock user (match cycle, admin
   * actions, enrollment) for up to 12s. Only take the lock for the brief
   * enrolled_user_count update below. */
  sdf_fingerprint_op_result_t res = fp_delete_user(user_id);
  if (res == SDF_FINGERPRINT_OP_OK) {
    SDF_LOCK_GUARD(guard, s_state.lock, SDF_SERVICES_LOCK_WAIT_MS);
    if (guard.acquired == pdTRUE && s_state.enrolled_user_count > 0) {
      s_state.enrolled_user_count--;
    }
  }
  if (res == SDF_FINGERPRINT_OP_OK) {
    sdf_storage_delete_user_name(user_id);
  }
  return (res == SDF_FINGERPRINT_OP_OK) ? ESP_OK : ESP_FAIL;
}

esp_err_t sdf_services_clear_all_users(void) {
  if (s_state.lock == NULL)
    return ESP_ERR_INVALID_STATE;
  /* See sdf_services_delete_user(): fp_delete_all_users() is a long
   * blocking UART call already serialized by the fingerprint driver's own
   * mutex, so don't hold s_state.lock across it. */
  sdf_fingerprint_op_result_t res = fp_delete_all_users();
  if (res == SDF_FINGERPRINT_OP_OK) {
    SDF_LOCK_GUARD(guard, s_state.lock, SDF_SERVICES_LOCK_WAIT_MS);
    if (guard.acquired == pdTRUE) {
      s_state.enrolled_user_count = 0;
    }
  }
  if (res == SDF_FINGERPRINT_OP_OK) {
    for (uint16_t i = SDF_FINGERPRINT_USER_ID_MIN; i <= SDF_SERVICES_MAX_USERS; i++) {
      sdf_storage_delete_user_name(i);
    }
  }
  return (res == SDF_FINGERPRINT_OP_OK) ? ESP_OK : ESP_FAIL;
}

esp_err_t sdf_services_query_users(uint16_t *user_ids, uint8_t *permissions,
                                   size_t *count, size_t max_count) {
  if (s_state.lock == NULL)
    return ESP_ERR_INVALID_STATE;
  /* See sdf_services_delete_user(): fp_query_users() is a long blocking
   * UART call already serialized by the fingerprint driver's own mutex, so
   * don't hold s_state.lock across it - there's no s_state to protect here
   * anyway, this call only touches the sensor. */
  sdf_fingerprint_op_result_t res =
      fp_query_users(user_ids, permissions, count, max_count);
  return (res == SDF_FINGERPRINT_OP_OK) ? ESP_OK : ESP_FAIL;
}

esp_err_t sdf_services_change_user_permission(uint16_t user_id,
                                              uint8_t permission) {
  if (user_id < SDF_FINGERPRINT_USER_ID_MIN ||
      user_id > SDF_FINGERPRINT_USER_ID_MAX || permission < 1u ||
      permission > 3u) {
    return ESP_ERR_INVALID_ARG;
  }

  if (s_state.lock == NULL || s_state.admin_action_done_sem == NULL) {
    return ESP_ERR_INVALID_STATE;
  }

  bool initialized = false;
  {
    SDF_LOCK_GUARD(guard, s_state.lock, SDF_SERVICES_LOCK_WAIT_MS);
    if (guard.acquired != pdTRUE) {
      return ESP_ERR_TIMEOUT;
    }

    initialized = s_state.initialized;
    if (!initialized || s_state.pending_admin_action != SDF_SERVICES_ADMIN_ACTION_NONE ||
        s_state.permission_change_pending ||
        s_state.enrollment_request_pending ||
        sdf_enrollment_sm_is_active(&s_state.enrollment)) {
      return ESP_ERR_INVALID_STATE;
    }
  }

  if (!initialized) {
    return ESP_ERR_INVALID_STATE;
  }

  const size_t query_capacity = (size_t)SDF_SERVICES_MAX_USERS;
  size_t count = query_capacity;
  esp_err_t err;

  if (esp_get_free_heap_size() < 4096) {
    ESP_LOGE(TAG, "Insufficient heap for permission change query");
    sdf_app_emit_audit(SDF_AUDIT_PROTOCOL_ERROR, 0, ESP_ERR_NO_MEM, 0);
    return ESP_ERR_NO_MEM;
  }

  /* Use local temp arrays for query, then pack into compact format */
  uint16_t user_ids[SDF_SERVICES_MAX_USERS];
  uint8_t perms[SDF_SERVICES_MAX_USERS];

  err = sdf_services_query_users(user_ids, perms, &count, query_capacity);
  if (err != ESP_OK) {
    return err;
  }

  /* Pack results into compact bitmap + packed permissions format */
  sdf_services_pack_user_list(user_ids, perms, count,
                              &s_perm_user_bmp,
                              s_perm_perm_packed);

  bool found = SDF_SERVICES_BMP_TEST(s_perm_user_bmp, user_id);
  uint8_t current_permission = found ? sdf_services_perm_get(s_perm_perm_packed, user_id) : 0;

  /* Count admins using packed permissions */
  size_t admin_count = 0;
  for (uint16_t id = 1; id <= SDF_SERVICES_MAX_USERS; id++) {
    if (SDF_SERVICES_BMP_TEST(s_perm_user_bmp, id) &&
        sdf_services_perm_get(s_perm_perm_packed, id) == 3u) {
      admin_count++;
    }
  }

  if (!found) {
    return ESP_ERR_NOT_FOUND;
  }

  if (current_permission == permission) {
    return ESP_OK;
  }

  if (current_permission == 3u && permission != 3u && admin_count <= 1u) {
    return ESP_ERR_INVALID_STATE;
  }

  {
    SDF_LOCK_GUARD(guard, s_state.lock, SDF_SERVICES_LOCK_WAIT_MS);
    if (guard.acquired != pdTRUE) {
      return ESP_ERR_TIMEOUT;
    }

    if (s_state.pending_admin_action != SDF_SERVICES_ADMIN_ACTION_NONE ||
        s_state.permission_change_pending ||
        s_state.enrollment_request_pending ||
        sdf_enrollment_sm_is_active(&s_state.enrollment)) {
      return ESP_ERR_INVALID_STATE;
    }

    while (xSemaphoreTake(s_state.admin_action_done_sem, 0) == pdTRUE) {
    }

    s_state.permission_change_pending = true;
    s_state.permission_change_user_id = user_id;
    s_state.permission_change_permission = permission;
    s_state.permission_change_result = ESP_ERR_TIMEOUT;
    s_state.pending_admin_action = SDF_SERVICES_ADMIN_ACTION_CHANGE_PERMISSION;
    s_state.pending_admin_action_start_us = esp_timer_get_time();
  }

  led_pulse_blue();
  if (s_state.wake_sem != NULL) {
    xSemaphoreGive(s_state.wake_sem);
  }

  if (xSemaphoreTake(s_state.admin_action_done_sem,
                     pdMS_TO_TICKS(SDF_SERVICES_PERMISSION_CHANGE_WAIT_MS)) !=
      pdTRUE) {
    return ESP_ERR_TIMEOUT;
  }

  {
    SDF_LOCK_GUARD(guard, s_state.lock, SDF_SERVICES_LOCK_WAIT_MS);
    if (guard.acquired != pdTRUE) {
      return ESP_ERR_TIMEOUT;
    }
    return s_state.permission_change_result;
  }
}

esp_err_t sdf_services_request_admin_action(sdf_services_admin_action_t action) {
  if (action == SDF_SERVICES_ADMIN_ACTION_NONE) {
    return ESP_ERR_INVALID_ARG;
  }

  if (s_state.lock == NULL || s_state.admin_action_done_sem == NULL) {
    return ESP_ERR_INVALID_STATE;
  }

  bool initialized = false;
  {
    SDF_LOCK_GUARD(guard, s_state.lock, SDF_SERVICES_LOCK_WAIT_MS);
    if (guard.acquired != pdTRUE) {
      return ESP_ERR_TIMEOUT;
    }

    initialized = s_state.initialized;
    if (!initialized || s_state.pending_admin_action != SDF_SERVICES_ADMIN_ACTION_NONE ||
        s_state.permission_change_pending ||
        s_state.enrollment_request_pending ||
        sdf_enrollment_sm_is_active(&s_state.enrollment)) {
      return ESP_ERR_INVALID_STATE;
    }
  }

  if (!initialized) {
    return ESP_ERR_INVALID_STATE;
  }

  {
    SDF_LOCK_GUARD(guard, s_state.lock, SDF_SERVICES_LOCK_WAIT_MS);
    if (guard.acquired != pdTRUE) {
      return ESP_ERR_TIMEOUT;
    }

    if (s_state.pending_admin_action != SDF_SERVICES_ADMIN_ACTION_NONE ||
        s_state.permission_change_pending ||
        s_state.enrollment_request_pending ||
        sdf_enrollment_sm_is_active(&s_state.enrollment)) {
      return ESP_ERR_INVALID_STATE;
    }

    while (xSemaphoreTake(s_state.admin_action_done_sem, 0) == pdTRUE) {
    }

    s_state.pending_admin_action = action;
    s_state.pending_admin_action_start_us = esp_timer_get_time();

    /* LED indication based on action type. This call path (a direct
     * sdf_services_request_admin_action() call, used by WEB_REG_AUTH and
     * NUKI_REPAIR) never emits SDF_EVENT_ROUTER_ADMIN_ACTION_REQUEST, so it
     * doesn't go through sdf_admin_task's own copy of this switch - mirrored
     * here rather than shared, consistent with sdf_button_cb's own separate
     * copy for the button-triggered path. */
    switch (action) {
      case SDF_SERVICES_ADMIN_ACTION_WEB_REG_AUTH:
        led_pulse_white();
        break;
      case SDF_SERVICES_ADMIN_ACTION_NUKI_REPAIR:
        led_pulse_cyan();
        break;
      default:
        break;
    }
  }

  if (s_state.wake_sem != NULL) {
    xSemaphoreGive(s_state.wake_sem);
  }

  return ESP_OK;
}

esp_err_t sdf_services_reset_state(void) {
  if (s_state.lock == NULL) {
    return ESP_ERR_INVALID_STATE;
  }

  if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(SDF_SERVICES_LOCK_WAIT_MS)) != pdTRUE) {
    return ESP_ERR_TIMEOUT;
  }

  // Reset all state variables to defaults
  s_state.enrolled_user_count = 0;
  s_state.failed_attempt_count = 0;
  s_state.lockout_until_us = 0;
  s_state.failed_attempt_window_start_us = 0;
  s_state.pending_admin_action = SDF_SERVICES_ADMIN_ACTION_NONE;
  s_state.pending_admin_action_start_us = 0;
  s_state.match_cooldown_until_us = 0;
  s_state.enrollment_request_pending = false;
  s_state.request_user_id = 0;
  s_state.request_permission = 0;
  s_state.permission_change_pending = false;
  s_state.permission_change_user_id = 0;
  s_state.permission_change_permission = 0;
  s_state.permission_change_result = ESP_OK;
  sdf_enrollment_sm_init(&s_state.enrollment);

  xSemaphoreGive(s_state.lock);

  // Turn off LED
  led_off();

  return ESP_OK;
}

esp_err_t sdf_services_request_enrollment(uint16_t user_id, uint8_t permission) {
  if (user_id < SDF_FINGERPRINT_USER_ID_MIN ||
      user_id > SDF_FINGERPRINT_USER_ID_MAX || permission < 1u ||
      permission > 3u) {
    return ESP_ERR_INVALID_ARG;
  }

  if (s_state.lock == NULL) {
    return ESP_ERR_INVALID_STATE;
  }

  if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(SDF_SERVICES_LOCK_WAIT_MS)) != pdTRUE) {
    return ESP_ERR_TIMEOUT;
  }

  if (!s_state.initialized || s_state.enrollment_request_pending ||
      sdf_enrollment_sm_is_active(&s_state.enrollment)) {
    xSemaphoreGive(s_state.lock);
    return ESP_ERR_INVALID_STATE;
  }

  s_state.request_user_id = user_id;
  s_state.request_permission = permission;
  s_state.enrollment_request_pending = true;
  xSemaphoreGive(s_state.lock);

  if (s_state.wake_sem != NULL) {
    xSemaphoreGive(s_state.wake_sem);
  }
  return ESP_OK;
}

sdf_services_setup_state_t sdf_services_get_setup_state(void) {
  size_t enrolled_user_count = 0;

  if (s_state.lock != NULL) {
    SDF_LOCK_GUARD(guard, s_state.lock, SDF_SERVICES_LOCK_WAIT_MS);
    if (guard.acquired == pdTRUE) {
      enrolled_user_count = s_state.enrolled_user_count;
    }
  }

  if (enrolled_user_count == 0) {
    return SDF_SERVICES_SETUP_STATE_UNCLAIMED;
  }

  uint32_t authorization_id = 0;
  uint8_t shared_key[32] = {0};
  if (sdf_storage_nuki_load(&authorization_id, shared_key) == ESP_OK) {
    return SDF_SERVICES_SETUP_STATE_CLAIMED_COMPLETE;
  }
  return SDF_SERVICES_SETUP_STATE_CLAIMED_INCOMPLETE;
}

bool sdf_services_is_enrollment_active(void) {
  if (s_state.lock == NULL) {
    return false;
  }

  bool active = false;
  {
    SDF_LOCK_GUARD(guard, s_state.lock, SDF_SERVICES_LOCK_WAIT_MS);
    if (guard.acquired == pdTRUE) {
      active = sdf_enrollment_sm_is_active(&s_state.enrollment) ||
               s_state.enrollment_request_pending;
    }
  }
  return active;
}

sdf_enrollment_sm_t sdf_services_get_enrollment_state(void) {
  sdf_enrollment_sm_t snapshot;
  sdf_enrollment_sm_init(&snapshot);

  if (s_state.lock == NULL) {
    return snapshot;
  }

  {
    SDF_LOCK_GUARD(guard, s_state.lock, SDF_SERVICES_LOCK_WAIT_MS);
    if (guard.acquired == pdTRUE) {
      snapshot = s_state.enrollment;
    }
  }
  return snapshot;
}

void sdf_services_start_pending_enrollment_if_any(void) {
  bool started = false;
  bool failed = false;

  if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(SDF_SERVICES_LOCK_WAIT_MS)) !=
      pdTRUE) {
    return;
  }

  if (!s_state.enrollment_request_pending ||
      sdf_enrollment_sm_is_active(&s_state.enrollment)) {
    xSemaphoreGive(s_state.lock);
    return;
  }

  esp_err_t err = sdf_enrollment_sm_start(&s_state.enrollment, s_state.request_user_id,
                                          s_state.request_permission);
  s_state.enrollment_request_pending = false;
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Unable to start enrollment state machine: %s",
             esp_err_to_name(err));
    failed = true;
    xSemaphoreGive(s_state.lock);
  } else {
    ESP_LOGI(TAG, "Enrollment started user_id=%u permission=%u step=%u cmd=0x%02X",
             (unsigned)s_state.enrollment.user_id,
             (unsigned)s_state.enrollment.permission,
             (unsigned)sdf_enrollment_sm_current_step(&s_state.enrollment),
             (unsigned)sdf_enrollment_sm_current_command(&s_state.enrollment));
    started = true;
    xSemaphoreGive(s_state.lock);
  }

  if (started) {
    esp_err_t power_hold_err = fp_set_keep_power_on(true);
    if (power_hold_err != ESP_OK) {
      ESP_LOGW(TAG, "Failed to enable enrollment power hold: %s",
               esp_err_to_name(power_hold_err));
    }
    led_pulse_blue();
    sdf_services_emit_enrollment_event(SDF_EVENT_ROUTER_ENROLLMENT_STEP_COMPLETE,
                                       &s_state.enrollment);
    return;
  }

  if (failed) {
    sdf_services_emit_enrollment_event(SDF_EVENT_ROUTER_ENROLLMENT_STEP_COMPLETE,
                                       &s_state.enrollment);
  }
}

esp_err_t sdf_services_get_web_reg_auth(char *username, size_t username_max,
                                         uint8_t *permission) {
  if (!username || !permission) {
    return ESP_ERR_INVALID_ARG;
  }

  if (s_state.lock == NULL) {
    return ESP_ERR_INVALID_STATE;
  }

  if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(SDF_SERVICES_LOCK_WAIT_MS)) != pdTRUE) {
    return ESP_ERR_TIMEOUT;
  }

  if (!s_state.web_reg_auth_pending || s_state.request_web_username[0] == '\0') {
    xSemaphoreGive(s_state.lock);
    return ESP_ERR_NOT_FOUND;
  }

  strncpy(username, s_state.request_web_username, username_max - 1);
  username[username_max - 1] = '\0';
  *permission = s_state.request_web_permission;

  xSemaphoreGive(s_state.lock);
  return ESP_OK;
}

void sdf_services_clear_web_reg_auth(void) {
  if (s_state.lock == NULL) {
    return;
  }

  if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(SDF_SERVICES_LOCK_WAIT_MS)) == pdTRUE) {
    s_state.web_reg_auth_pending = false;
    s_state.request_web_username[0] = '\0';
    xSemaphoreGive(s_state.lock);
  }
}

esp_err_t sdf_services_set_web_reg_auth(const char *username,
                                         const uint8_t *password_hash,
                                         size_t hash_len) {
  if (!username || !password_hash || hash_len != SDF_STORAGE_WEB_USER_HASH_LEN) {
    return ESP_ERR_INVALID_ARG;
  }

  if (s_state.lock == NULL) {
    return ESP_ERR_INVALID_STATE;
  }

  if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(SDF_SERVICES_LOCK_WAIT_MS)) != pdTRUE) {
    return ESP_ERR_TIMEOUT;
  }

  if (s_state.web_reg_auth_pending) {
    xSemaphoreGive(s_state.lock);
    return ESP_ERR_INVALID_STATE;
  }

  strncpy(s_state.request_web_username, username, SDF_STORAGE_WEB_USER_NAME_MAX - 1);
  s_state.request_web_username[SDF_STORAGE_WEB_USER_NAME_MAX - 1] = '\0';
  memcpy(s_state.request_web_password_hash, password_hash, hash_len);
  s_state.request_web_permission = 1;
  s_state.web_reg_auth_pending = true;

  xSemaphoreGive(s_state.lock);
  return ESP_OK;
}

esp_err_t sdf_services_get_web_reg_password_hash(uint8_t *password_hash,
                                                  size_t hash_len) {
  if (!password_hash || hash_len != SDF_STORAGE_WEB_USER_HASH_LEN) {
    return ESP_ERR_INVALID_ARG;
  }

  if (s_state.lock == NULL) {
    return ESP_ERR_INVALID_STATE;
  }

  if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(SDF_SERVICES_LOCK_WAIT_MS)) != pdTRUE) {
    return ESP_ERR_TIMEOUT;
  }

  if (!s_state.web_reg_auth_pending) {
    xSemaphoreGive(s_state.lock);
    return ESP_ERR_NOT_FOUND;
  }

  memcpy(password_hash, s_state.request_web_password_hash, hash_len);
  xSemaphoreGive(s_state.lock);
  return ESP_OK;
}
