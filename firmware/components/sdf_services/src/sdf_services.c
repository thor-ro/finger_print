#include "sdf_services_internal.h"
#include "sdf_drivers.h"

#include <stdlib.h>
#include <string.h>

#ifdef CONFIG_IDF_TARGET_LINUX
#include "sdf_mock_linux_gpio.h"
#else
#include "button_gpio.h"
#include "driver/gpio.h"
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

#define SDF_SERVICES_TASK_NAME "sdf_fp"
#define SDF_SERVICES_TASK_STACK 4096
#define SDF_SERVICES_TASK_PRIORITY 5

#define SDF_SERVICES_DEFAULT_UART_PORT 1
#define SDF_SERVICES_DEFAULT_UART_TX_PIN 4
#define SDF_SERVICES_DEFAULT_UART_RX_PIN 5
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

static const char *TAG = "sdf_services";

static sdf_services_state_t s_state = {0};

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

static esp_err_t sdf_services_fingerprint_result_to_err(
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

static void sdf_services_complete_permission_change(esp_err_t result) {
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
sdf_services_notify_security_event(const sdf_services_security_event_t *event) {
  if (event == NULL || s_state.lock == NULL) {
    return;
  }

  sdf_services_security_event_cb cb = NULL;
  void *ctx = NULL;
  {
    SDF_LOCK_GUARD(guard, s_state.lock, SDF_SERVICES_LOCK_WAIT_MS);
    if (guard.acquired != pdTRUE)
      return;
    cb = s_state.config.security_event_cb;
    ctx = s_state.config.security_event_ctx;
  }

  if (cb != NULL) {
    cb(ctx, event);
  }
}

static void sdf_services_execute_admin_action(
    sdf_services_admin_action_t action,
    sdf_services_admin_action_cb action_cb, void *action_ctx) {
  ESP_LOGI(TAG, "Authorized action %d!", (int)action);

  if (action == SDF_SERVICES_ADMIN_ACTION_ENROLL) {
    const size_t max_users = (size_t)SDF_FINGERPRINT_USER_ID_MAX + 1u;
    uint16_t *users = calloc(max_users, sizeof(*users));
    uint8_t *perms = calloc(max_users, sizeof(*perms));
    size_t count = 0;
    uint16_t new_id = 0;
    esp_err_t err = ESP_OK;

    if (users == NULL || perms == NULL) {
      ESP_LOGE(TAG, "Failed to allocate admin enrollment query buffers");
    } else {
      err = sdf_services_query_users(users, perms, &count, max_users);
      if (err == ESP_OK) {
        for (uint16_t id = 1; id <= SDF_FINGERPRINT_USER_ID_MAX; id++) {
          bool in_use = false;
          for (size_t i = 0; i < count; i++) {
            if (users[i] == id) {
              in_use = true;
              break;
            }
          }
          if (!in_use) {
            new_id = id;
            break;
          }
        }
      } else {
        ESP_LOGW(TAG, "Failed to query users for local enrollment: %s",
                 esp_err_to_name(err));
      }
    }

    free(perms);
    free(users);

    if (new_id > 0) {
      sdf_services_request_enrollment(new_id, 1);
    } else {
      if (err == ESP_OK) {
        ESP_LOGW(TAG, "No free user IDs available for local enrollment");
      }
      led_flash_red();
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

  if (action_cb != NULL) {
    action_cb(action_ctx, action);
  }
}

#ifndef CONFIG_IDF_TARGET_LINUX
static void sdf_services_btn_cb(void *arg, void *usr_data) {
  sdf_services_admin_action_t action =
      (sdf_services_admin_action_t)(uintptr_t)usr_data;

  if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(SDF_SERVICES_LOCK_WAIT_MS)) !=
      pdTRUE) {
    return;
  }

  // If there are 0 users, there is no admin to authorize.
  // Execute the requested action immediately.
  if (s_state.enrolled_user_count == 0) {
    s_state.pending_admin_action = SDF_SERVICES_ADMIN_ACTION_NONE;
    s_state.pending_admin_action_start_us = 0;
    xSemaphoreGive(s_state.lock);

    if (action == SDF_SERVICES_ADMIN_ACTION_ENROLL) {
      led_pulse_blue();
      sdf_services_request_enrollment(1, 3);
    } else {
      // For other actions (pairing, reset, etc.), trigger the callback immediately
      sdf_services_admin_action_cb action_cb = s_state.config.admin_action_cb;
      void *action_ctx = s_state.config.admin_action_ctx;
      if (action_cb != NULL) {
        action_cb(action_ctx, action);
      }
    }
    return;
  }

  // Set the pending action and wait for Admin fingerprint
  if (s_state.pending_admin_action == SDF_SERVICES_ADMIN_ACTION_NONE) {
    s_state.pending_admin_action = action;
    s_state.pending_admin_action_start_us = esp_timer_get_time();

    switch (action) {
    case SDF_SERVICES_ADMIN_ACTION_ENROLL:
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
    ESP_LOGI(TAG, "Hardware button pressed, pending admin action: %d",
             (int)action);
  }

  xSemaphoreGive(s_state.lock);
}
#endif

static void sdf_services_run_match_cycle(void) {
  sdf_services_unlock_cb unlock_cb = NULL;
  void *unlock_ctx = NULL;
  uint32_t cooldown_ms = SDF_SERVICES_DEFAULT_MATCH_COOLDOWN_MS;
  uint32_t failed_attempt_threshold =
      SDF_SERVICES_DEFAULT_FAILED_ATTEMPT_THRESHOLD;
  uint32_t failed_attempt_window_ms =
      SDF_SERVICES_DEFAULT_FAILED_ATTEMPT_WINDOW_MS;
  uint32_t lockout_duration_ms = SDF_SERVICES_DEFAULT_LOCKOUT_DURATION_MS;
  int64_t now_us = esp_timer_get_time();
  bool lockout_cleared = false;

  if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(SDF_SERVICES_LOCK_WAIT_MS)) !=
      pdTRUE) {
    return;
  }

  // If there are no enrolled users, matching is impossible and polling the
  // sensor will just result in 12-second timeouts waiting for a finger.
  // We can safely skip the match cycle entirely.
  if (s_state.enrolled_user_count == 0) {
    xSemaphoreGive(s_state.lock);
    return;
  }

  if (s_state.lockout_until_us > 0 && now_us >= s_state.lockout_until_us) {
    s_state.lockout_until_us = 0;
    s_state.failed_attempt_count = 0;
    s_state.failed_attempt_window_start_us = 0;
    lockout_cleared = true;
  }

  if (now_us < s_state.match_cooldown_until_us ||
      sdf_enrollment_sm_is_active(&s_state.enrollment) ||
      s_state.enrollment_request_pending || now_us < s_state.lockout_until_us) {
    xSemaphoreGive(s_state.lock);
    if (lockout_cleared) {
      sdf_services_security_event_t event = {
          .type = SDF_SERVICES_SECURITY_EVENT_LOCKOUT_CLEARED,
          .user_id = 0,
          .failed_attempts = 0,
          .lockout_remaining_ms = 0,
      };
      sdf_services_notify_security_event(&event);
    }
    return;
  }

  unlock_cb = s_state.config.unlock_cb;
  unlock_ctx = s_state.config.unlock_ctx;
  cooldown_ms = s_state.config.match_cooldown_ms;
  failed_attempt_threshold = s_state.config.failed_attempt_threshold;
  failed_attempt_window_ms = s_state.config.failed_attempt_window_ms;
  lockout_duration_ms = s_state.config.lockout_duration_ms;
  xSemaphoreGive(s_state.lock);

  if (lockout_cleared) {
    sdf_services_security_event_t event = {
        .type = SDF_SERVICES_SECURITY_EVENT_LOCKOUT_CLEARED,
        .user_id = 0,
        .failed_attempts = 0,
        .lockout_remaining_ms = 0,
    };
    sdf_services_notify_security_event(&event);
  }

  if (unlock_cb == NULL) {
    return;
  }

  sdf_fingerprint_match_t match = {0};
  sdf_fingerprint_op_result_t match_result =
      fp_match_1n(&match);
  if (match_result == SDF_FINGERPRINT_OP_NO_MATCH ||
      match_result == SDF_FINGERPRINT_OP_TIMEOUT) {
    bool emit_failed_attempt = false;
    bool emit_lockout = false;
    uint32_t failed_attempts = 0;
    uint32_t lockout_remaining_ms = 0;

    if (xSemaphoreTake(s_state.lock,
                       pdMS_TO_TICKS(SDF_SERVICES_LOCK_WAIT_MS)) == pdTRUE) {
      s_state.match_cooldown_until_us =
          now_us + ((int64_t)cooldown_ms * 1000LL);

      if (match_result == SDF_FINGERPRINT_OP_NO_MATCH) {
        if (s_state.failed_attempt_window_start_us == 0 ||
            (now_us - s_state.failed_attempt_window_start_us) >
                ((int64_t)failed_attempt_window_ms * 1000LL)) {
          s_state.failed_attempt_window_start_us = now_us;
          s_state.failed_attempt_count = 0;
        }

        s_state.failed_attempt_count++;
        failed_attempts = s_state.failed_attempt_count;
        emit_failed_attempt = true;

        if (s_state.failed_attempt_count >= failed_attempt_threshold) {
          s_state.lockout_until_us =
              now_us + ((int64_t)lockout_duration_ms * 1000LL);
          s_state.failed_attempt_count = 0;
          s_state.failed_attempt_window_start_us = 0;
          lockout_remaining_ms = lockout_duration_ms;
          emit_lockout = true;
        }
      }
      xSemaphoreGive(s_state.lock);
    }

    if (emit_failed_attempt) {
      sdf_services_security_event_t event = {
          .type = SDF_SERVICES_SECURITY_EVENT_MATCH_FAILED,
          .user_id = 0,
          .failed_attempts = failed_attempts,
          .lockout_remaining_ms = lockout_remaining_ms,
      };
      sdf_services_notify_security_event(&event);
    }

    if (emit_lockout) {
      sdf_services_security_event_t event = {
          .type = SDF_SERVICES_SECURITY_EVENT_LOCKOUT_ENTERED,
          .user_id = 0,
          .failed_attempts = failed_attempt_threshold,
          .lockout_remaining_ms = lockout_remaining_ms,
      };
      sdf_services_notify_security_event(&event);
    }
    return;
  }

  if (match_result != SDF_FINGERPRINT_OP_OK) {
    ESP_LOGW(TAG, "Fingerprint match error: %s",
             sdf_services_fingerprint_result_name(match_result));
    if (xSemaphoreTake(s_state.lock,
                       pdMS_TO_TICKS(SDF_SERVICES_LOCK_WAIT_MS)) == pdTRUE) {
      s_state.match_cooldown_until_us =
          now_us + ((int64_t)cooldown_ms * 1000LL);
      xSemaphoreGive(s_state.lock);
    }
    return;
  }

  ESP_LOGI(TAG, "Fingerprint match user_id=%u permission=%u",
           (unsigned)match.user_id, (unsigned)match.permission);

  sdf_services_admin_action_t pending_action =
      SDF_SERVICES_ADMIN_ACTION_NONE;
  sdf_services_admin_action_cb pending_action_cb = NULL;
  void *pending_action_ctx = NULL;
  bool has_pending_admin_action = false;
  bool claimed_pending_admin_action = false;
  if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(SDF_SERVICES_LOCK_WAIT_MS)) ==
      pdTRUE) {
    has_pending_admin_action =
        (s_state.pending_admin_action != SDF_SERVICES_ADMIN_ACTION_NONE);
    if (has_pending_admin_action && match.permission == 3) {
      pending_action = s_state.pending_admin_action;
      pending_action_cb = s_state.config.admin_action_cb;
      pending_action_ctx = s_state.config.admin_action_ctx;
      s_state.pending_admin_action = SDF_SERVICES_ADMIN_ACTION_NONE;
      s_state.pending_admin_action_start_us = 0;
      claimed_pending_admin_action = true;
    }
    xSemaphoreGive(s_state.lock);
  }

  if (has_pending_admin_action) {
    if (claimed_pending_admin_action) {
      ESP_LOGI(TAG,
               "Pending admin action %d consumed by user_id=%u permission=%u",
               (int)pending_action, (unsigned)match.user_id,
               (unsigned)match.permission);
      sdf_services_execute_admin_action(pending_action, pending_action_cb,
                                        pending_action_ctx);
    } else {
      ESP_LOGW(TAG,
               "Ignoring normal unlock for user_id=%u permission=%u while "
               "admin action is pending",
               (unsigned)match.user_id, (unsigned)match.permission);
      led_flash_red();
    }
    return;
  }

  int unlock_result = unlock_cb(unlock_ctx, match.user_id);
  if (unlock_result != 0) {
    ESP_LOGW(TAG, "Unlock callback returned %d for user_id=%u", unlock_result,
             (unsigned)match.user_id);
  }

  if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(SDF_SERVICES_LOCK_WAIT_MS)) ==
      pdTRUE) {
    s_state.match_cooldown_until_us = now_us + ((int64_t)cooldown_ms * 1000LL);
    s_state.failed_attempt_count = 0;
    s_state.failed_attempt_window_start_us = 0;
    xSemaphoreGive(s_state.lock);
  }

  sdf_services_security_event_t event = {
      .type = SDF_SERVICES_SECURITY_EVENT_MATCH_SUCCEEDED,
      .user_id = match.user_id,
      .failed_attempts = 0,
      .lockout_remaining_ms = 0,
  };
  sdf_services_notify_security_event(&event);
}

static void IRAM_ATTR sdf_services_wake_isr(void *arg) {
  (void)arg;
  BaseType_t higher_priority_task_woken = pdFALSE;
  xSemaphoreGiveFromISR(s_state.wake_sem, &higher_priority_task_woken);
  if (higher_priority_task_woken == pdTRUE) {
#ifdef CONFIG_IDF_TARGET_LINUX
    portYIELD_FROM_ISR(pdTRUE);
#else
    portYIELD_FROM_ISR();
#endif
  }
}

static void sdf_services_run_admin_auth_cycle(void) {
  if (s_state.pending_admin_action == SDF_SERVICES_ADMIN_ACTION_NONE) {
    return;
  }

  sdf_fingerprint_match_t match = {0};
  sdf_fingerprint_op_result_t match_result =
      fp_match_1n(&match);

  if (match_result == SDF_FINGERPRINT_OP_NO_MATCH ||
      match_result == SDF_FINGERPRINT_OP_TIMEOUT) {
    return;
  }

  if (match_result != SDF_FINGERPRINT_OP_OK) {
    ESP_LOGW(TAG, "Fingerprint match error in admin auth: %s",
             sdf_services_fingerprint_result_name(match_result));
    return;
  }

  ESP_LOGI(TAG, "Admin Auth Match: user_id=%u, permission=%u",
           (unsigned)match.user_id, (unsigned)match.permission);
  if (match.permission == 3) {
    sdf_services_admin_action_t action;
    sdf_services_admin_action_cb action_cb = NULL;
    void *action_ctx = NULL;

    if (xSemaphoreTake(s_state.lock,
                       pdMS_TO_TICKS(SDF_SERVICES_LOCK_WAIT_MS)) == pdTRUE) {
      action = s_state.pending_admin_action;
      action_cb = s_state.config.admin_action_cb;
      action_ctx = s_state.config.admin_action_ctx;

      // Clear pending action
      s_state.pending_admin_action = SDF_SERVICES_ADMIN_ACTION_NONE;
      s_state.pending_admin_action_start_us = 0;
      xSemaphoreGive(s_state.lock);

      sdf_services_execute_admin_action(action, action_cb, action_ctx);
    }
  } else {
    ESP_LOGW(TAG, "Admin auth match permission %u != ADMIN(3), rejecting.",
             (unsigned)match.permission);
    led_flash_red();
  }
}

static void sdf_services_task(void *arg) {
  (void)arg;
  bool is_powered = true;

#ifndef CONFIG_IDF_TARGET_LINUX
  esp_task_wdt_add(NULL);
#endif

  // Wait for the initialization to complete before querying users
  while (!sdf_services_is_ready()) {
    vTaskDelay(pdMS_TO_TICKS(10));
#ifndef CONFIG_IDF_TARGET_LINUX
    esp_task_wdt_reset();
#endif
  }

  // Reset watchdog before boot query – the fingerprint sensor query can take
  // up to 12 s when the sensor is unresponsive (UART read timeout).  Without
  // this reset the accumulated time from init spin-wait + query can exceed
  // the 15 s TWDT limit and cause a SW_CPU reset.
#ifndef CONFIG_IDF_TARGET_LINUX
  esp_task_wdt_reset();
#endif

  // Fast connectivity check – retries with increasing power-settle delays.
  // If the sensor never responds, skip the slow user query entirely.
  esp_err_t probe_err = fp_probe();

#ifndef CONFIG_IDF_TARGET_LINUX
  esp_task_wdt_reset();
#endif

  // Check unclaimed state on boot and breathe white LED if true
  uint16_t users[1];
  uint8_t perms[1];
  size_t count = 0;
  esp_err_t query_err = ESP_FAIL;
  if (probe_err == ESP_OK) {
    query_err = sdf_services_query_users(users, perms, &count, 1);
  } else {
    ESP_LOGW(TAG, "Skipping user query – sensor probe failed");
  }
  
  if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(SDF_SERVICES_LOCK_WAIT_MS)) == pdTRUE) {
    s_state.enrolled_user_count = count;
    xSemaphoreGive(s_state.lock);
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
    uint32_t poll_interval_ms = SDF_SERVICES_DEFAULT_MATCH_POLL_MS;
    int64_t now_us = esp_timer_get_time();
    bool is_pending_admin_action = false;
    sdf_services_admin_action_t timed_out_action =
        SDF_SERVICES_ADMIN_ACTION_NONE;

    if (xSemaphoreTake(s_state.lock,
                       pdMS_TO_TICKS(SDF_SERVICES_LOCK_WAIT_MS)) == pdTRUE) {
      poll_interval_ms = s_state.config.match_poll_interval_ms;

      // Check for config button timeout
      if (s_state.pending_admin_action != SDF_SERVICES_ADMIN_ACTION_NONE &&
          s_state.pending_admin_action_start_us > 0) {
        if ((now_us - s_state.pending_admin_action_start_us) >
            ((int64_t)SDF_SERVICES_ADMIN_ACTION_TIMEOUT_MS * 1000LL)) {
          ESP_LOGW(TAG, "Admin Action Timeout. Resetting state.");
          timed_out_action = s_state.pending_admin_action;
          s_state.pending_admin_action = SDF_SERVICES_ADMIN_ACTION_NONE;
          s_state.pending_admin_action_start_us = 0;
        }
      }

      is_pending_admin_action =
          (s_state.pending_admin_action != SDF_SERVICES_ADMIN_ACTION_NONE);
      xSemaphoreGive(s_state.lock);
    }

    if (timed_out_action != SDF_SERVICES_ADMIN_ACTION_NONE) {
      led_flash_red();
      if (timed_out_action == SDF_SERVICES_ADMIN_ACTION_CHANGE_PERMISSION) {
        sdf_services_complete_permission_change(ESP_ERR_TIMEOUT);
      }
    }

    sdf_services_start_pending_enrollment_if_any();

    if (is_pending_admin_action) {
      sdf_services_run_admin_auth_cycle();
    } else {
      sdf_services_run_enrollment_step();
      sdf_services_run_match_cycle();
    }

    bool should_block = false;
    if (xSemaphoreTake(s_state.lock,
                       pdMS_TO_TICKS(SDF_SERVICES_LOCK_WAIT_MS)) == pdTRUE) {
      int64_t now_us = esp_timer_get_time();
      should_block = !sdf_enrollment_sm_is_active(&s_state.enrollment) &&
                     !s_state.enrollment_request_pending &&
                     (s_state.match_cooldown_until_us == 0 || now_us >= s_state.match_cooldown_until_us) &&
                     (s_state.lockout_until_us == 0 || now_us >= s_state.lockout_until_us);
      xSemaphoreGive(s_state.lock);
    }

    if (should_block && s_state.config.wake_gpio >= 0) {
      if (is_powered) {
        led_off();
        fp_set_power(false);
        is_powered = false;
      }
#ifndef CONFIG_IDF_TARGET_LINUX
      esp_task_wdt_delete(NULL);
#endif
      xSemaphoreTake(s_state.wake_sem, portMAX_DELAY);
#ifndef CONFIG_IDF_TARGET_LINUX
      esp_task_wdt_add(NULL);
#endif
      if (!is_powered) {
        fp_set_power(true);
        is_powered = true;
        vTaskDelay(
            pdMS_TO_TICKS(200)); /* Boot delay for FP sensor after power up */
      }
    } else {
      vTaskDelay(pdMS_TO_TICKS(poll_interval_ms));
    }
  }
}

void sdf_services_get_default_config(sdf_services_config_t *config) {
  if (config == NULL) {
    return;
  }

  memset(config, 0, sizeof(*config));
  config->fingerprint.uart_port = SDF_SERVICES_DEFAULT_UART_PORT;
  config->fingerprint.tx_pin = SDF_SERVICES_DEFAULT_UART_TX_PIN;
  config->fingerprint.rx_pin = SDF_SERVICES_DEFAULT_UART_RX_PIN;
  config->fingerprint.baud_rate = SDF_SERVICES_DEFAULT_UART_BAUD_RATE;
  config->fingerprint.response_timeout_ms =
      SDF_SERVICES_DEFAULT_UART_TIMEOUT_MS;
  config->fingerprint.rx_buffer_size = SDF_SERVICES_DEFAULT_UART_RX_BUFFER;
  config->fingerprint.tx_buffer_size = SDF_SERVICES_DEFAULT_UART_TX_BUFFER;

  config->match_poll_interval_ms = SDF_SERVICES_DEFAULT_MATCH_POLL_MS;
  config->match_cooldown_ms = SDF_SERVICES_DEFAULT_MATCH_COOLDOWN_MS;
  config->failed_attempt_threshold =
      SDF_SERVICES_DEFAULT_FAILED_ATTEMPT_THRESHOLD;
  config->failed_attempt_window_ms =
      SDF_SERVICES_DEFAULT_FAILED_ATTEMPT_WINDOW_MS;
  config->lockout_duration_ms = SDF_SERVICES_DEFAULT_LOCKOUT_DURATION_MS;
  config->unlock_cb = NULL;
  config->unlock_ctx = NULL;
  config->enrollment_ctx = NULL;
  config->security_event_cb = NULL;
  config->security_event_ctx = NULL;
  config->wake_gpio = -1;
  config->power_en_gpio = -1;
  config->enrollment_btn_gpio = -1;
  config->ws2812_led_gpio = -1;
  config->battery_adc_pin = -1;
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
    if (s_state.lock == NULL || s_state.wake_sem == NULL ||
        s_state.admin_action_done_sem == NULL) {
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
  };

  esp_err_t err = sdf_drivers_init(&drivers_config);
  if (err != ESP_OK) {
    return err;
  }

  BaseType_t task_ok = xTaskCreate(sdf_services_task, SDF_SERVICES_TASK_NAME,
                                   SDF_SERVICES_TASK_STACK, NULL,
                                   SDF_SERVICES_TASK_PRIORITY, &s_state.task);
  if (task_ok != pdPASS) {
    sdf_drivers_deinit();
    return ESP_FAIL;
  }

  if (config->wake_gpio >= 0) {
    gpio_config_t io_config = {
        .pin_bit_mask = (1ULL << config->wake_gpio),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    err = gpio_config(&io_config);
    if (err == ESP_OK) {
      gpio_install_isr_service(0);
      gpio_isr_handler_add(config->wake_gpio, sdf_services_wake_isr, NULL);
    } else {
      ESP_LOGW(TAG, "Failed to configure wake GPIO interrupt: %s",
               esp_err_to_name(err));
    }
  }

#ifndef CONFIG_IDF_TARGET_LINUX
  if (config->enrollment_btn_gpio >= 0) {
    button_config_t btn_config = {
        .long_press_time = 3000,
        .short_press_time = 180,
    };
    button_gpio_config_t gpio_config = {
        .gpio_num = config->enrollment_btn_gpio,
        .active_level = 0,
        .enable_power_save = false,
        .disable_pull = false,
    };
    if (iot_button_new_gpio_device(&btn_config, &gpio_config,
                                   &s_state.btn_handle) == ESP_OK) {
      iot_button_register_cb(s_state.btn_handle, BUTTON_SINGLE_CLICK, NULL,
                             sdf_services_btn_cb,
                             (void *)SDF_SERVICES_ADMIN_ACTION_ENROLL);
      iot_button_register_cb(s_state.btn_handle, BUTTON_DOUBLE_CLICK, NULL,
                             sdf_services_btn_cb,
                             (void *)SDF_SERVICES_ADMIN_ACTION_NUKI_PAIR);

      button_event_args_t arg_3s = {.long_press = {.press_time = 3000}};
      iot_button_register_cb(s_state.btn_handle, BUTTON_LONG_PRESS_START,
                             &arg_3s, sdf_services_btn_cb,
                             (void *)SDF_SERVICES_ADMIN_ACTION_ZB_JOIN);

      button_event_args_t arg_8s = {.long_press = {.press_time = 8000}};
      iot_button_register_cb(s_state.btn_handle, BUTTON_LONG_PRESS_START,
                             &arg_8s, sdf_services_btn_cb,
                             (void *)SDF_SERVICES_ADMIN_ACTION_FACTORY_RESET);
    } else {
      ESP_LOGW(TAG, "Failed to create enrollment iot_button");
    }
  }
#endif

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
  sdf_fingerprint_op_result_t res;
  {
    SDF_LOCK_GUARD(guard, s_state.lock, SDF_SERVICES_LOCK_WAIT_MS);
    if (guard.acquired != pdTRUE)
      return ESP_ERR_TIMEOUT;
    res = fp_delete_user(user_id);
    if (res == SDF_FINGERPRINT_OP_OK && s_state.enrolled_user_count > 0) {
      s_state.enrolled_user_count--;
    }
  }
  return (res == SDF_FINGERPRINT_OP_OK) ? ESP_OK : ESP_FAIL;
}

esp_err_t sdf_services_clear_all_users(void) {
  if (s_state.lock == NULL)
    return ESP_ERR_INVALID_STATE;
  sdf_fingerprint_op_result_t res;
  {
    SDF_LOCK_GUARD(guard, s_state.lock, SDF_SERVICES_LOCK_WAIT_MS);
    if (guard.acquired != pdTRUE)
      return ESP_ERR_TIMEOUT;
    res = fp_delete_all_users();
    if (res == SDF_FINGERPRINT_OP_OK) {
      s_state.enrolled_user_count = 0;
    }
  }
  return (res == SDF_FINGERPRINT_OP_OK) ? ESP_OK : ESP_FAIL;
}

esp_err_t sdf_services_query_users(uint16_t *user_ids, uint8_t *permissions,
                                   size_t *count, size_t max_count) {
  if (s_state.lock == NULL)
    return ESP_ERR_INVALID_STATE;
  sdf_fingerprint_op_result_t res;
  {
    SDF_LOCK_GUARD(guard, s_state.lock, SDF_SERVICES_LOCK_WAIT_MS);
    if (guard.acquired != pdTRUE)
      return ESP_ERR_TIMEOUT;
    res = fp_query_users(user_ids, permissions, count, max_count);
  }
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

  const size_t query_capacity = (size_t)SDF_FINGERPRINT_USER_ID_MAX + 1u;
  uint16_t *user_ids = calloc(query_capacity, sizeof(*user_ids));
  uint8_t *permissions = calloc(query_capacity, sizeof(*permissions));
  if (user_ids == NULL || permissions == NULL) {
    free(permissions);
    free(user_ids);
    return ESP_ERR_NO_MEM;
  }

  size_t count = query_capacity;
  esp_err_t err =
      sdf_services_query_users(user_ids, permissions, &count, query_capacity);
  if (err != ESP_OK) {
    free(permissions);
    free(user_ids);
    return err;
  }

  bool found = false;
  uint8_t current_permission = 0;
  size_t admin_count = 0;
  for (size_t i = 0; i < count; ++i) {
    if (permissions[i] == 3u) {
      admin_count++;
    }
    if (user_ids[i] == user_id) {
      found = true;
      current_permission = permissions[i];
    }
  }

  free(permissions);
  free(user_ids);

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
