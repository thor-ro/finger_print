#include "sdf_services_internal.h"

#include "sdf_drivers.h"

#include "esp_log.h"
#include "esp_timer.h"
#ifndef CONFIG_IDF_TARGET_LINUX
#include "esp_task_wdt.h"
#endif

static const char *TAG = "sdf_services";

static const char *
sdf_services_enrollment_state_name(sdf_enrollment_state_t state) {
  switch (state) {
  case SDF_ENROLLMENT_STATE_IDLE:
    return "IDLE";
  case SDF_ENROLLMENT_STATE_STEP_1:
    return "STEP_1";
  case SDF_ENROLLMENT_STATE_STEP_2:
    return "STEP_2";
  case SDF_ENROLLMENT_STATE_STEP_3:
    return "STEP_3";
  case SDF_ENROLLMENT_STATE_SUCCESS:
    return "SUCCESS";
  case SDF_ENROLLMENT_STATE_ERROR:
    return "ERROR";
  default:
    return "UNKNOWN";
  }
}

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

static void sdf_services_notify_enrollment(void) {
  sdf_services_state_t *state = sdf_services_state();
  if (state->lock == NULL) {
    return;
  }

  sdf_services_enrollment_cb cb = NULL;
  void *ctx = NULL;
  sdf_enrollment_sm_t snapshot;
  {
    SDF_LOCK_GUARD(guard, state->lock, SDF_SERVICES_LOCK_WAIT_MS);
    if (guard.acquired != pdTRUE) {
      return;
    }
    cb = state->config.enrollment_cb;
    ctx = state->config.enrollment_ctx;
    snapshot = state->enrollment;
  }

  if (cb != NULL) {
    cb(ctx, &snapshot);
  }
}

static void
sdf_services_handle_enrollment_feedback(const sdf_enrollment_sm_t *before,
                                        const sdf_enrollment_sm_t *after) {
  if (before == NULL || after == NULL) {
    return;
  }

  if (after->state == SDF_ENROLLMENT_STATE_SUCCESS) {
    led_enrollment_success_green();
    return;
  }

  if (after->state == SDF_ENROLLMENT_STATE_ERROR) {
    led_flash_red();
    return;
  }

  if (after->completed_steps > before->completed_steps) {
    led_enrollment_step_green();
  }
}

void sdf_services_run_enrollment_step(void) {
  sdf_services_state_t *state = sdf_services_state();

  while (true) {
    if (xSemaphoreTake(state->lock, pdMS_TO_TICKS(SDF_SERVICES_LOCK_WAIT_MS)) !=
        pdTRUE) {
      return;
    }

    if (!sdf_enrollment_sm_is_active(&state->enrollment)) {
      xSemaphoreGive(state->lock);
      return;
    }

    sdf_enrollment_sm_t before = state->enrollment;
    uint8_t step = sdf_enrollment_sm_current_step(&state->enrollment);
    uint8_t cmd = sdf_enrollment_sm_current_command(&state->enrollment);
    uint16_t user_id = state->enrollment.user_id;
    uint8_t permission = state->enrollment.permission;
    xSemaphoreGive(state->lock);

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

#ifndef CONFIG_IDF_TARGET_LINUX
    esp_task_wdt_reset();
#endif
    sdf_fingerprint_op_result_t step_result =
        fp_enroll_step(driver_step, user_id, permission);

    if (xSemaphoreTake(state->lock, pdMS_TO_TICKS(SDF_SERVICES_LOCK_WAIT_MS)) !=
        pdTRUE) {
      return;
    }

    sdf_enrollment_sm_apply_step_result(&state->enrollment, step_result);
    sdf_enrollment_sm_t after = state->enrollment;
    xSemaphoreGive(state->lock);

    bool retry_same_step = step_result == SDF_FINGERPRINT_OP_FAILED &&
                           after.state == before.state &&
                           after.completed_steps == before.completed_steps;
    ESP_LOGI(TAG,
             "Enrollment step=%u cmd=0x%02X user_id=%u permission=%u "
             "before=%s completed=%u driver=%s after=%s result=%s "
             "completed=%u%s",
             (unsigned)step, (unsigned)cmd, (unsigned)user_id,
             (unsigned)permission,
             sdf_services_enrollment_state_name(before.state),
             (unsigned)before.completed_steps,
             sdf_services_fingerprint_result_name(step_result),
             sdf_services_enrollment_state_name(after.state),
             sdf_services_enrollment_result_name(after.result),
             (unsigned)after.completed_steps,
             retry_same_step ? " (retrying same step after ACK_FAIL)" : "");

    if (!retry_same_step && after.state != before.state &&
        sdf_enrollment_sm_is_active(&after)) {
      ESP_LOGI(TAG,
               "Enrollment advancing immediately to step=%u cmd=0x%02X after "
               "ACK success",
               (unsigned)sdf_enrollment_sm_current_step(&after),
               (unsigned)sdf_enrollment_sm_current_command(&after));
    }

    sdf_services_handle_enrollment_feedback(&before, &after);
    sdf_services_notify_enrollment();

    if (after.state == SDF_ENROLLMENT_STATE_SUCCESS ||
        after.state == SDF_ENROLLMENT_STATE_ERROR) {
      ESP_LOGI(TAG,
               "Enrollment finished user_id=%u permission=%u state=%s "
               "result=%s completed=%u/3",
               (unsigned)after.user_id, (unsigned)after.permission,
               sdf_services_enrollment_state_name(after.state),
               sdf_services_enrollment_result_name(after.result),
               (unsigned)after.completed_steps);

      if (xSemaphoreTake(state->lock, pdMS_TO_TICKS(SDF_SERVICES_LOCK_WAIT_MS)) ==
          pdTRUE) {
        state->match_cooldown_until_us =
            esp_timer_get_time() +
            ((int64_t)state->config.match_cooldown_ms * 1000LL);

        if (after.state == SDF_ENROLLMENT_STATE_SUCCESS) {
          state->enrolled_user_count++;
        }
        xSemaphoreGive(state->lock);
      }
      fp_set_keep_power_on(false);
      return;
    }

    if (step_result != SDF_FINGERPRINT_OP_OK) {
      return;
    }
  }
}

void sdf_services_start_pending_enrollment_if_any(void) {
  sdf_services_state_t *state = sdf_services_state();
  bool started = false;
  bool failed = false;

  if (xSemaphoreTake(state->lock, pdMS_TO_TICKS(SDF_SERVICES_LOCK_WAIT_MS)) !=
      pdTRUE) {
    return;
  }

  if (!state->enrollment_request_pending ||
      sdf_enrollment_sm_is_active(&state->enrollment)) {
    xSemaphoreGive(state->lock);
    return;
  }

  esp_err_t err = sdf_enrollment_sm_start(&state->enrollment, state->request_user_id,
                                          state->request_permission);
  state->enrollment_request_pending = false;
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Unable to start enrollment state machine: %s",
             esp_err_to_name(err));
    failed = true;
    xSemaphoreGive(state->lock);
  } else {
    ESP_LOGI(TAG, "Enrollment started user_id=%u permission=%u step=%u cmd=0x%02X",
             (unsigned)state->enrollment.user_id,
             (unsigned)state->enrollment.permission,
             (unsigned)sdf_enrollment_sm_current_step(&state->enrollment),
             (unsigned)sdf_enrollment_sm_current_command(&state->enrollment));
    started = true;
    xSemaphoreGive(state->lock);
  }

  if (started) {
    esp_err_t power_hold_err = fp_set_keep_power_on(true);
    if (power_hold_err != ESP_OK) {
      ESP_LOGW(TAG, "Failed to enable enrollment power hold: %s",
               esp_err_to_name(power_hold_err));
    }
    led_pulse_blue();
    sdf_services_notify_enrollment();
    return;
  }

  if (failed) {
    sdf_services_notify_enrollment();
  }
}

esp_err_t sdf_services_request_enrollment(uint16_t user_id,
                                          uint8_t permission) {
  sdf_services_state_t *state = sdf_services_state();

  if (user_id < SDF_FINGERPRINT_USER_ID_MIN ||
      user_id > SDF_FINGERPRINT_USER_ID_MAX || permission < 1u ||
      permission > 3u) {
    return ESP_ERR_INVALID_ARG;
  }

  if (state->lock == NULL) {
    return ESP_ERR_INVALID_STATE;
  }

  if (xSemaphoreTake(state->lock, pdMS_TO_TICKS(SDF_SERVICES_LOCK_WAIT_MS)) !=
      pdTRUE) {
    return ESP_ERR_TIMEOUT;
  }

  if (!state->initialized || state->enrollment_request_pending ||
      sdf_enrollment_sm_is_active(&state->enrollment)) {
    xSemaphoreGive(state->lock);
    return ESP_ERR_INVALID_STATE;
  }

  state->request_user_id = user_id;
  state->request_permission = permission;
  state->enrollment_request_pending = true;
  xSemaphoreGive(state->lock);

  if (state->wake_sem != NULL) {
    xSemaphoreGive(state->wake_sem);
  }
  return ESP_OK;
}

bool sdf_services_is_enrollment_active(void) {
  sdf_services_state_t *state = sdf_services_state();
  if (state->lock == NULL) {
    return false;
  }

  bool active = false;
  {
    SDF_LOCK_GUARD(guard, state->lock, SDF_SERVICES_LOCK_WAIT_MS);
    if (guard.acquired == pdTRUE) {
      active = sdf_enrollment_sm_is_active(&state->enrollment) ||
               state->enrollment_request_pending;
    }
  }
  return active;
}

sdf_enrollment_sm_t sdf_services_get_enrollment_state(void) {
  sdf_services_state_t *state = sdf_services_state();
  sdf_enrollment_sm_t snapshot;
  sdf_enrollment_sm_init(&snapshot);

  if (state->lock == NULL) {
    return snapshot;
  }

  {
    SDF_LOCK_GUARD(guard, state->lock, SDF_SERVICES_LOCK_WAIT_MS);
    if (guard.acquired == pdTRUE) {
      snapshot = state->enrollment;
    }
  }
  return snapshot;
}
