#include "sdf_services.h"
#include "sdf_lock_guard.h"

#include <string.h>

#ifdef CONFIG_IDF_TARGET_LINUX
#include "sdf_mock_linux_services.h"
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
#ifndef CONFIG_IDF_TARGET_LINUX
#include "led_strip.h"
#endif
#include "sdkconfig.h"

#define SDF_SERVICES_TASK_NAME "sdf_fp"
#define SDF_SERVICES_TASK_STACK 4096
#define SDF_SERVICES_TASK_PRIORITY 5

#define SDF_SERVICES_LOCK_WAIT_MS 250u

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

/*
 * LED command values are vendor-module specific. These are best-effort defaults
 * and can be tuned without changing the enrollment flow.
 */
#define SDF_SERVICES_LED_MODE_BREATH 0x01u
#define SDF_SERVICES_LED_MODE_FLASH 0x02u
#define SDF_SERVICES_LED_MODE_SOLID 0x03u
#define SDF_SERVICES_LED_COLOR_RED 0x01u
#define SDF_SERVICES_LED_COLOR_GREEN 0x02u
#define SDF_SERVICES_LED_COLOR_BLUE 0x04u
#define SDF_SERVICES_LED_COLOR_ORANGE 0x08u

static const char *TAG = "sdf_services";

typedef struct {
  SemaphoreHandle_t lock;
  SemaphoreHandle_t wake_sem;
  TaskHandle_t task;
  bool initialized;
  sdf_services_config_t config;
  sdf_enrollment_sm_t enrollment;
  bool enrollment_request_pending;
  uint16_t request_user_id;
  uint8_t request_permission;
  int64_t match_cooldown_until_us;
  uint32_t failed_attempt_count;
  int64_t failed_attempt_window_start_us;
  int64_t lockout_until_us;
  led_strip_handle_t led_strip;
  sdf_services_admin_action_t pending_admin_action;
  int64_t pending_admin_action_start_us;
#ifndef CONFIG_IDF_TARGET_LINUX
  button_handle_t btn_handle;
#endif
} sdf_services_state_t;

static sdf_services_state_t s_state = {0};

static const char *
sdf_services_enrollment_result_name(sdf_enrollment_result_t result) {
  switch (result) {
  case SDF_ENROLLMENT_RESULT_NONE:
    return "NONE";
  case SDF_ENROLLMENT_RESULT_SUCCESS:
    return "SUCCESS";
  case SDF_ENROLLMENT_RESULT_FAILED:
    return "FAILED";
  case SDF_ENROLLMENT_RESULT_TIMEOUT:
    return "TIMEOUT";
  case SDF_ENROLLMENT_RESULT_FULL:
    return "FULL";
  case SDF_ENROLLMENT_RESULT_USER_OCCUPIED:
    return "USER_OCCUPIED";
  case SDF_ENROLLMENT_RESULT_FINGER_OCCUPIED:
    return "FINGER_OCCUPIED";
  case SDF_ENROLLMENT_RESULT_PROTOCOL_ERROR:
    return "PROTOCOL_ERROR";
  case SDF_ENROLLMENT_RESULT_IO_ERROR:
    return "IO_ERROR";
  case SDF_ENROLLMENT_RESULT_BAD_ARG:
    return "BAD_ARG";
  case SDF_ENROLLMENT_RESULT_BUSY:
    return "BUSY";
  default:
    return "UNKNOWN";
  }
}

static const char *
sdf_services_fingerprint_result_name(sdf_fingerprint_op_result_t result) {
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

static void sdf_services_notify_enrollment(void) {
  if (s_state.lock == NULL) {
    return;
  }

  sdf_services_enrollment_cb cb = NULL;
  void *ctx = NULL;
  sdf_enrollment_sm_t snapshot;
  {
    SDF_LOCK_GUARD(guard, s_state.lock, SDF_SERVICES_LOCK_WAIT_MS);
    if (guard.acquired != pdTRUE)
      return;
    cb = s_state.config.enrollment_cb;
    ctx = s_state.config.enrollment_ctx;
    snapshot = s_state.enrollment;
  }

  if (cb != NULL) {
    cb(ctx, &snapshot);
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

static void sdf_services_try_set_led(uint8_t mode, uint8_t color,
                                     uint8_t cycles) {
  if (s_state.led_strip == NULL) {
    return;
  }

  uint8_t r = 0, g = 0, b = 0;
  if (color & SDF_SERVICES_LED_COLOR_RED) {
    r = 255;
  }
  if (color & SDF_SERVICES_LED_COLOR_GREEN) {
    g = 255;
  }
  if (color & SDF_SERVICES_LED_COLOR_BLUE) {
    b = 255;
  }
  if (color == SDF_SERVICES_LED_COLOR_ORANGE) {
    r = 255;
    g = 165;
    b = 0;
  }

  if (mode == SDF_SERVICES_LED_MODE_BREATH ||
      mode == SDF_SERVICES_LED_MODE_FLASH) {
    for (uint8_t i = 0; i < cycles; i++) {
      led_strip_set_pixel(s_state.led_strip, 0, r, g, b);
      led_strip_refresh(s_state.led_strip);
      vTaskDelay(pdMS_TO_TICKS(500));
      led_strip_clear(s_state.led_strip);
      vTaskDelay(pdMS_TO_TICKS(500));
    }
  } else if (mode == SDF_SERVICES_LED_MODE_SOLID) {
    if (cycles > 0) { // Using cycles as roughly half seconds to leave it on
      led_strip_set_pixel(s_state.led_strip, 0, r, g, b);
      led_strip_refresh(s_state.led_strip);
      vTaskDelay(pdMS_TO_TICKS(cycles * 500));
      led_strip_clear(s_state.led_strip);
    }
  }
}

static void
sdf_services_handle_enrollment_feedback(const sdf_enrollment_sm_t *before,
                                        const sdf_enrollment_sm_t *after) {
  if (before == NULL || after == NULL) {
    return;
  }

  if (after->state == SDF_ENROLLMENT_STATE_SUCCESS) {
    sdf_services_try_set_led(SDF_SERVICES_LED_MODE_SOLID,
                             SDF_SERVICES_LED_COLOR_GREEN, 2);
    return;
  }

  if (after->state == SDF_ENROLLMENT_STATE_ERROR) {
    sdf_services_try_set_led(SDF_SERVICES_LED_MODE_FLASH,
                             SDF_SERVICES_LED_COLOR_RED, 3);
    return;
  }

  if (after->completed_steps > before->completed_steps) {
    uint8_t flashes = after->completed_steps;
    if (flashes == 0) {
      flashes = 1;
    }
    sdf_services_try_set_led(SDF_SERVICES_LED_MODE_FLASH,
                             SDF_SERVICES_LED_COLOR_GREEN, flashes);
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

  // If there are 0 users, a single click just goes straight to enrollment
  // because there is no admin to authorize yet.
  if (action == SDF_SERVICES_ADMIN_ACTION_ENROLL) {
    uint16_t users[1];
    uint8_t perms[1];
    size_t count = 0;

    // Quick query without taking lock again (nested) is tricky since
    // sdf_services_query_users takes the lock. We are in a callback.
    // However, the driver itself can be queried.
    sdf_fingerprint_driver_query_users(users, perms, &count, 1);
    if (count == 0) {
      s_state.pending_admin_action = SDF_SERVICES_ADMIN_ACTION_NONE;
      s_state.pending_admin_action_start_us = 0;
      xSemaphoreGive(s_state.lock);

      // Trigger enrollment immediately for Admin (ID 1, Perm 3)
      sdf_services_try_set_led(SDF_SERVICES_LED_MODE_SOLID,
                               SDF_SERVICES_LED_COLOR_GREEN, 1);
      sdf_services_request_enrollment(1, 3);
      return;
    }
  }

  // Set the pending action and wait for Admin fingerprint
  if (s_state.pending_admin_action == SDF_SERVICES_ADMIN_ACTION_NONE) {
    s_state.pending_admin_action = action;
    s_state.pending_admin_action_start_us = esp_timer_get_time();

    switch (action) {
    case SDF_SERVICES_ADMIN_ACTION_ENROLL:
      sdf_services_try_set_led(SDF_SERVICES_LED_MODE_FLASH,
                               SDF_SERVICES_LED_COLOR_BLUE, 3);
      break;
    case SDF_SERVICES_ADMIN_ACTION_NUKI_PAIR:
      sdf_services_try_set_led(SDF_SERVICES_LED_MODE_FLASH,
                               SDF_SERVICES_LED_COLOR_RED |
                                   SDF_SERVICES_LED_COLOR_GREEN,
                               3); // Yellow ~ Red+Green
      break;
    case SDF_SERVICES_ADMIN_ACTION_ZB_JOIN:
      sdf_services_try_set_led(SDF_SERVICES_LED_MODE_FLASH,
                               SDF_SERVICES_LED_COLOR_RED |
                                   SDF_SERVICES_LED_COLOR_BLUE,
                               3); // Purple ~ Red+Blue
      break;
    case SDF_SERVICES_ADMIN_ACTION_FACTORY_RESET:
      sdf_services_try_set_led(SDF_SERVICES_LED_MODE_FLASH,
                               SDF_SERVICES_LED_COLOR_RED, 3);
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
      sdf_fingerprint_driver_match_1n(&match);
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

static void sdf_services_run_enrollment_step(void) {
  if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(SDF_SERVICES_LOCK_WAIT_MS)) !=
      pdTRUE) {
    return;
  }

  if (!sdf_enrollment_sm_is_active(&s_state.enrollment)) {
    xSemaphoreGive(s_state.lock);
    return;
  }

  sdf_enrollment_sm_t before = s_state.enrollment;
  uint8_t step = sdf_enrollment_sm_current_step(&s_state.enrollment);
  uint16_t user_id = s_state.enrollment.user_id;
  uint8_t permission = s_state.enrollment.permission;
  xSemaphoreGive(s_state.lock);

  sdf_fingerprint_enroll_step_t driver_step = SDF_FINGERPRINT_ENROLL_STEP_1;
  switch (step) {
  case 1:
    driver_step = SDF_FINGERPRINT_ENROLL_STEP_1;
    break;
  case 2:
    driver_step = SDF_FINGERPRINT_ENROLL_STEP_2;
    break;
  case 3:
    driver_step = SDF_FINGERPRINT_ENROLL_STEP_3;
    break;
  default:
    return;
  }

  sdf_fingerprint_op_result_t step_result =
      sdf_fingerprint_driver_enroll_step(driver_step, user_id, permission);

  if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(SDF_SERVICES_LOCK_WAIT_MS)) !=
      pdTRUE) {
    return;
  }

  sdf_enrollment_sm_apply_step_result(&s_state.enrollment, step_result);
  sdf_enrollment_sm_t after = s_state.enrollment;
  xSemaphoreGive(s_state.lock);

  ESP_LOGI(TAG, "Enrollment step %u result=%s state=%d completed=%u",
           (unsigned)step, sdf_services_fingerprint_result_name(step_result),
           (int)after.state, (unsigned)after.completed_steps);

  sdf_services_handle_enrollment_feedback(&before, &after);
  sdf_services_notify_enrollment();

  if (after.state == SDF_ENROLLMENT_STATE_SUCCESS ||
      after.state == SDF_ENROLLMENT_STATE_ERROR) {
    ESP_LOGI(TAG, "Enrollment finished for user_id=%u (%s)",
             (unsigned)after.user_id,
             sdf_services_enrollment_result_name(after.result));

    if (xSemaphoreTake(s_state.lock,
                       pdMS_TO_TICKS(SDF_SERVICES_LOCK_WAIT_MS)) == pdTRUE) {
      s_state.match_cooldown_until_us =
          esp_timer_get_time() +
          ((int64_t)s_state.config.match_cooldown_ms * 1000LL);
      xSemaphoreGive(s_state.lock);
    }
  }
}

static void sdf_services_start_pending_enrollment_if_any(void) {
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

  esp_err_t err = sdf_enrollment_sm_start(
      &s_state.enrollment, s_state.request_user_id, s_state.request_permission);
  s_state.enrollment_request_pending = false;
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Unable to start enrollment state machine: %s",
             esp_err_to_name(err));
    failed = true;
    xSemaphoreGive(s_state.lock);
  } else {
    ESP_LOGI(TAG, "Enrollment started for user_id=%u permission=%u",
             (unsigned)s_state.enrollment.user_id,
             (unsigned)s_state.enrollment.permission);
    started = true;
    xSemaphoreGive(s_state.lock);
  }

  if (started) {
    sdf_services_try_set_led(SDF_SERVICES_LED_MODE_BREATH,
                             SDF_SERVICES_LED_COLOR_BLUE, 1);
    sdf_services_notify_enrollment();
    return;
  }

  if (failed) {
    sdf_services_notify_enrollment();
  }
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
      sdf_fingerprint_driver_match_1n(&match);

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

      ESP_LOGI(TAG, "Authorized action %d!", (int)action);
      sdf_services_try_set_led(SDF_SERVICES_LED_MODE_SOLID,
                               SDF_SERVICES_LED_COLOR_GREEN, 1);

      if (action == SDF_SERVICES_ADMIN_ACTION_ENROLL) {
        uint16_t users[SDF_FINGERPRINT_USER_ID_MAX + 1];
        uint8_t perms[SDF_FINGERPRINT_USER_ID_MAX + 1];
        size_t count = 0;
        uint16_t new_id = 0;

        esp_err_t err = sdf_services_query_users(
            users, perms, &count, SDF_FINGERPRINT_USER_ID_MAX + 1);
        if (err == ESP_OK) {
          bool used[SDF_FINGERPRINT_USER_ID_MAX + 1] = {false};
          for (size_t i = 0; i < count; i++) {
            if (users[i] <= SDF_FINGERPRINT_USER_ID_MAX)
              used[users[i]] = true;
          }
          for (uint16_t id = 1; id <= SDF_FINGERPRINT_USER_ID_MAX; id++) {
            if (!used[id]) {
              new_id = id;
              break;
            }
          }
        }

        if (new_id > 0) {
          sdf_services_request_enrollment(new_id, 1);
        } else {
          ESP_LOGW(TAG, "No free user IDs available for local enrollment");
          sdf_services_try_set_led(SDF_SERVICES_LED_MODE_FLASH,
                                   SDF_SERVICES_LED_COLOR_RED, 3);
        }
      } else {
        if (action_cb != NULL) {
          action_cb(action_ctx, action);
        }
      }
    }
  } else {
    ESP_LOGW(TAG, "Admin auth match permission %u != ADMIN(3), rejecting.",
             (unsigned)match.permission);
    sdf_services_try_set_led(SDF_SERVICES_LED_MODE_FLASH,
                             SDF_SERVICES_LED_COLOR_RED, 2);
  }
}

static void sdf_services_task(void *arg) {
  (void)arg;
  bool is_powered = true;

#ifndef CONFIG_IDF_TARGET_LINUX
  esp_task_wdt_add(NULL);
#endif

  while (true) {
#ifndef CONFIG_IDF_TARGET_LINUX
    esp_task_wdt_reset();
#endif
    uint32_t poll_interval_ms = SDF_SERVICES_DEFAULT_MATCH_POLL_MS;
    int64_t now_us = esp_timer_get_time();
    bool is_pending_admin_action = false;

    if (xSemaphoreTake(s_state.lock,
                       pdMS_TO_TICKS(SDF_SERVICES_LOCK_WAIT_MS)) == pdTRUE) {
      poll_interval_ms = s_state.config.match_poll_interval_ms;

      // Check for config button timeout
      if (s_state.pending_admin_action != SDF_SERVICES_ADMIN_ACTION_NONE &&
          s_state.pending_admin_action_start_us > 0) {
        if ((now_us - s_state.pending_admin_action_start_us) > 10000000LL) {
          ESP_LOGW(TAG, "Admin Action Timeout. Resetting state.");
          s_state.pending_admin_action = SDF_SERVICES_ADMIN_ACTION_NONE;
          s_state.pending_admin_action_start_us = 0;
          xSemaphoreGive(s_state.lock);
          sdf_services_try_set_led(SDF_SERVICES_LED_MODE_FLASH,
                                   SDF_SERVICES_LED_COLOR_RED, 3);
          if (xSemaphoreTake(s_state.lock,
                             pdMS_TO_TICKS(SDF_SERVICES_LOCK_WAIT_MS)) !=
              pdTRUE) {
            // best effort
          }
        }
      }

      is_pending_admin_action =
          (s_state.pending_admin_action != SDF_SERVICES_ADMIN_ACTION_NONE);
      xSemaphoreGive(s_state.lock);
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
      should_block = !sdf_enrollment_sm_is_active(&s_state.enrollment) &&
                     !s_state.enrollment_request_pending &&
                     s_state.match_cooldown_until_us == 0 &&
                     s_state.lockout_until_us == 0;
      xSemaphoreGive(s_state.lock);
    }

    if (should_block && s_state.config.wake_gpio >= 0) {
      if (is_powered) {
        sdf_fingerprint_driver_set_power(false);
        is_powered = false;
      }
      xSemaphoreTake(s_state.wake_sem, portMAX_DELAY);
      if (!is_powered) {
        sdf_fingerprint_driver_set_power(true);
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
    if (s_state.lock == NULL || s_state.wake_sem == NULL) {
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
  xSemaphoreGive(s_state.lock);

  esp_err_t err = sdf_fingerprint_driver_init(&s_state.config.fingerprint);
  if (err != ESP_OK) {
    return err;
  }

  BaseType_t task_ok = xTaskCreate(sdf_services_task, SDF_SERVICES_TASK_NAME,
                                   SDF_SERVICES_TASK_STACK, NULL,
                                   SDF_SERVICES_TASK_PRIORITY, &s_state.task);
  if (task_ok != pdPASS) {
    sdf_fingerprint_driver_deinit();
    return ESP_FAIL;
  }

  if (config->wake_gpio >= 0) {
    gpio_config_t io_config = {
        .pin_bit_mask = (1ULL << config->wake_gpio),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
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
  sdf_services_try_set_led(SDF_SERVICES_LED_MODE_FLASH,
                           SDF_SERVICES_LED_COLOR_ORANGE, 5);
}

esp_err_t sdf_services_request_enrollment(uint16_t user_id,
                                          uint8_t permission) {
  if (user_id < SDF_FINGERPRINT_USER_ID_MIN ||
      user_id > SDF_FINGERPRINT_USER_ID_MAX || permission < 1u ||
      permission > 3u) {
    return ESP_ERR_INVALID_ARG;
  }

  if (s_state.lock == NULL) {
    return ESP_ERR_INVALID_STATE;
  }

  if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(SDF_SERVICES_LOCK_WAIT_MS)) !=
      pdTRUE) {
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

  /* Wake the service task if it is blocked on the fingerprint semaphore */
  if (s_state.wake_sem != NULL) {
    xSemaphoreGive(s_state.wake_sem);
  }
  return ESP_OK;
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

esp_err_t sdf_services_delete_user(uint16_t user_id) {
  if (s_state.lock == NULL)
    return ESP_ERR_INVALID_STATE;
  sdf_fingerprint_op_result_t res;
  {
    SDF_LOCK_GUARD(guard, s_state.lock, SDF_SERVICES_LOCK_WAIT_MS);
    if (guard.acquired != pdTRUE)
      return ESP_ERR_TIMEOUT;
    res = sdf_fingerprint_driver_delete_user(user_id);
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
    res = sdf_fingerprint_driver_delete_all_users();
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
    res = sdf_fingerprint_driver_query_users(user_ids, permissions, count,
                                             max_count);
  }
  return (res == SDF_FINGERPRINT_OP_OK) ? ESP_OK : ESP_FAIL;
}
