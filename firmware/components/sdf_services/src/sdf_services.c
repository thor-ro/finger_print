#include "sdf_services_internal.h"
#include "sdf_drivers.h"
#include "sdf_platform.h"
#include "sdf_config.h"
#include "sdf_event_router.h"
#include "sdf_app.h"
#include "sdf_storage.h"

#include "esp_system.h"
#include "nvs.h"

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
/* The permission-change wait is served in slices, feeding the caller's task
 * watchdog between them. A single 15 s block is exactly the configured TWDT
 * window (sdf_config wdt_timeout_ms), so a scan that never arrives used to
 * panic-reboot the device from the sdf_app task, which subscribes to the
 * watchdog and feeds it only at the top of its loop. The slice is short
 * enough that any plausible window is fed many times over, and
 * sdf_platform_time_wdt_reset() is a no-op for a task that never
 * subscribed - so this is safe for every caller, not just sdf_app. */
#define SDF_SERVICES_PERMISSION_CHANGE_SLICE_MS 500u

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
 * Packed perms: 2 bits per user (0=none, 1=std, 2=elev, 3=admin)
 *
 * The persisted, authoritative copy of this representation lives in
 * s_state.enrolled_user_bmp/enrolled_perm_packed (see
 * sdf_services_internal.h) rather than as throwaway module statics here -
 * sdf_services_query_users() now serves callers directly from that cache
 * instead of a live sensor query (see cache-enrolled-user-state). */

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

const char *sdf_services_um_outcome_name(sdf_services_um_outcome_t outcome) {
  switch (outcome) {
  case SDF_SERVICES_UM_OK:
    return "ok";
  case SDF_SERVICES_UM_NOT_FOUND:
    return "not_found";
  case SDF_SERVICES_UM_ID_OCCUPIED:
    return "id_occupied";
  case SDF_SERVICES_UM_LAST_ADMIN:
    return "last_admin";
  case SDF_SERVICES_UM_NAME_TAKEN:
    return "name_taken";
  case SDF_SERVICES_UM_BUSY:
    return "busy";
  case SDF_SERVICES_UM_DENIED:
    return "denied";
  case SDF_SERVICES_UM_TIMEOUT:
    return "timeout";
  case SDF_SERVICES_UM_INVALID:
    return "invalid";
  case SDF_SERVICES_UM_FAILED:
    return "failed";
  case SDF_SERVICES_UM_UNAVAILABLE:
    return "unavailable";
  default:
    return "failed";
  }
}

bool sdf_services_um_action_is_remote(sdf_services_admin_action_t action) {
  return action == SDF_SERVICES_ADMIN_ACTION_DELETE_USER ||
         action == SDF_SERVICES_ADMIN_ACTION_REMOTE_ENROLL ||
         action == SDF_SERVICES_ADMIN_ACTION_RENAME_USER;
}

void sdf_services_record_um_action_timeout_locked(
    sdf_services_admin_action_t action) {
  if (!sdf_services_um_action_is_remote(action)) {
    return;
  }
  s_state.um_action_result = SDF_SERVICES_UM_TIMEOUT;
  s_state.um_action_result_user_id = s_state.pending_admin_action_user_id;
  s_state.um_action_result_permission = s_state.pending_admin_action_permission;
  s_state.um_action_result_valid = true;
}

/* Records a resolved remote delete/enroll outcome for
 * sdf_services_take_um_action_result(). Takes the lock itself - callers run
 * on the match task with no lock held. */
static void sdf_services_record_um_result(uint16_t user_id, uint8_t permission,
                                          sdf_services_um_outcome_t outcome) {
  if (s_state.lock == NULL) {
    return;
  }
  if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(SDF_SERVICES_LOCK_WAIT_MS)) == pdTRUE) {
    s_state.um_action_result = outcome;
    s_state.um_action_result_user_id = user_id;
    s_state.um_action_result_permission = permission;
    s_state.um_action_result_valid = true;
    xSemaphoreGive(s_state.lock);
  }
}

/* Shared guard evaluation for the remote delete/enroll requests: refuses
 * with a named outcome before the gate is armed, so an impossible or
 * premature request never pulses the pending-action LED or asks anyone to
 * scan (companion-user-mgmt). `is_enroll` selects which direction the
 * enrolled-state check applies (delete requires the target enrolled;
 * enrol requires it free). Callable with s_state.lock held. */
static sdf_services_um_outcome_t
sdf_services_um_remote_guards_locked(bool initialized, uint16_t user_id,
                                     uint8_t permission, bool is_enroll) {
  if (!initialized) {
    return SDF_SERVICES_UM_UNAVAILABLE;
  }

  if (user_id < SDF_FINGERPRINT_USER_ID_MIN ||
      user_id > SDF_SERVICES_MAX_USERS ||
      (is_enroll && (permission < 1u || permission > 3u))) {
    return SDF_SERVICES_UM_INVALID;
  }

  if (s_state.pending_admin_action != SDF_SERVICES_ADMIN_ACTION_NONE ||
      s_state.permission_change_pending ||
      s_state.enrollment_request_pending ||
      sdf_enrollment_sm_is_active(&s_state.enrollment)) {
    return SDF_SERVICES_UM_BUSY;
  }

  bool enrolled = SDF_SERVICES_BMP_TEST(s_state.enrolled_user_bmp, user_id);
  if (!is_enroll && !enrolled) {
    /* delete: the target must be enrolled */
    return SDF_SERVICES_UM_NOT_FOUND;
  }
  if (is_enroll && enrolled) {
    /* enrol: the target id must be free */
    return SDF_SERVICES_UM_ID_OCCUPIED;
  }

  /* Last-admin guard, from the same cached snapshot every other user-scoped
   * guard uses - only a deletion can hit this. */
  if (!is_enroll) {
    size_t admin_count = 0;
    for (uint16_t id = 1; id <= SDF_SERVICES_MAX_USERS; id++) {
      if (SDF_SERVICES_BMP_TEST(s_state.enrolled_user_bmp, id) &&
          sdf_services_perm_get(s_state.enrolled_perm_packed, id) == 3u) {
        admin_count++;
      }
    }
    if (sdf_services_perm_get(s_state.enrolled_perm_packed, user_id) == 3u &&
        admin_count <= 1u) {
      return SDF_SERVICES_UM_LAST_ADMIN;
    }
  }

  return SDF_SERVICES_UM_OK;
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
  sdf_event_router_emit(&evt, SDF_EVENT_ROUTER_EMIT_TIMEOUT_DEFAULT_MS);
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
  uint16_t enrollment_user_bmp = 0;
  uint8_t enrollment_perm_packed[SDF_SERVICES_PERM_PACKED_SIZE] = {0};

  /* sdf_services_query_users() is now cache-backed (see
   * cache-enrolled-user-state) - this is an in-RAM read under a mutex, not a
   * sensor UART round trip. */
  err = sdf_services_query_users(user_ids, perms, &count, max_users);
  if (err == ESP_OK) {
    /* Pack results into compact bitmap + packed permissions format */
    sdf_services_pack_user_list(user_ids, perms, count,
                                &enrollment_user_bmp,
                                enrollment_perm_packed);

    /* Find first free ID using bitmap (O(1) per check) */
    for (uint16_t id = 1; id <= SDF_SERVICES_MAX_USERS; id++) {
      if (!SDF_SERVICES_BMP_TEST(enrollment_user_bmp, id)) {
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

  sdf_services_um_outcome_t enroll_outcome =
      sdf_services_request_enrollment(new_id, permission);
  if (enroll_outcome != SDF_SERVICES_UM_OK) {
    ESP_LOGW(TAG,
             "Failed to queue local enrollment for user_id=%u permission=%u: "
             "%s",
             (unsigned)new_id, (unsigned)permission,
             sdf_services_um_outcome_name(enroll_outcome));
    led_flash_red();
  }
}

void sdf_services_pulse_pending_action_led(sdf_services_admin_action_t action) {
  switch (action) {
    case SDF_SERVICES_ADMIN_ACTION_NONE:
    case SDF_SERVICES_ADMIN_ACTION_CHANGE_PERMISSION:
      break;
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
    case SDF_SERVICES_ADMIN_ACTION_WEB_REG_AUTH:
      led_pulse_white();
      break;
    case SDF_SERVICES_ADMIN_ACTION_NUKI_REPAIR:
    case SDF_SERVICES_ADMIN_ACTION_BLE_PAIRING_WINDOW:
      led_pulse_cyan();
      break;
    case SDF_SERVICES_ADMIN_ACTION_DELETE_USER:
    case SDF_SERVICES_ADMIN_ACTION_REMOTE_ENROLL:
    case SDF_SERVICES_ADMIN_ACTION_RENAME_USER:
      /* A user-scoped, scan-gated action; white like WEB_REG_AUTH. The
       * mapping stays exhaustive over the action set (sdf-services-tasks). */
      led_pulse_white();
      break;
    default:
      break;
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
      /* Sensor-side permission change succeeded; update the persisted cache
       * and write it to NVS before reporting success, so the sensor and the
       * cache/NVS never disagree (see cache-enrolled-user-state). */
      bool persisted = false;
      {
        SDF_LOCK_GUARD(guard, s_state.lock, SDF_SERVICES_LOCK_WAIT_MS);
        if (guard.acquired == pdTRUE) {
          uint8_t prev_permission =
              sdf_services_perm_get(s_state.enrolled_perm_packed, user_id);
          sdf_services_perm_set(s_state.enrolled_perm_packed, user_id, permission);
          persisted = (sdf_services_persist_enrolled_users_locked() == ESP_OK);
          if (!persisted) {
            /* Retries exhausted: leave the cache reflecting the last
             * successfully persisted permission (stale-but-safe), no
             * sensor-side rollback per design.md. */
            sdf_services_perm_set(s_state.enrolled_perm_packed, user_id,
                                  prev_permission);
          }
        } else {
          err = ESP_ERR_TIMEOUT;
        }
      }

      if (persisted) {
        ESP_LOGI(TAG, "Changed fingerprint permission for user_id=%u to %u",
                 (unsigned)user_id, (unsigned)permission);
        led_flash_green();
      } else {
        ESP_LOGE(TAG,
                 "Failed to persist enrolled-user cache after changing "
                 "permission for user_id=%u",
                 (unsigned)user_id);
        led_flash_red();
        err = ESP_FAIL;
      }
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
    /* username/permission are no longer carried on this event - the sole
     * consumer (sdf_app_on_web_reg_auth_result) reads them back from this
     * same owned pending-request state via sdf_services_get_web_reg_auth(),
     * so no lock is needed here to build the payload. */
    sdf_event_router_event_t evt = {
        .type = SDF_EVENT_ROUTER_WEB_REG_AUTH_RESULT,
        .priority = SDF_EVENT_ROUTER_PRIO_HIGH,
        .timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL),
        .payload.web_reg_auth_result.authorized = true,
    };
    sdf_event_router_emit(&evt, SDF_EVENT_ROUTER_EMIT_TIMEOUT_DEFAULT_MS);
    return;
  }

  if (action == SDF_SERVICES_ADMIN_ACTION_DELETE_USER) {
    uint16_t user_id = 0;
    {
      SDF_LOCK_GUARD(guard, s_state.lock, SDF_SERVICES_LOCK_WAIT_MS);
      if (guard.acquired == pdTRUE) {
        user_id = s_state.pending_admin_action_user_id;
      }
    }

    /* The last-admin guard lives inside delete_user() and is evaluated
     * from the cache snapshot BEFORE any sensor operation - re-checked
     * here at execution time because enrolled state may have changed since
     * the request armed the gate (companion-user-mgmt). */
    sdf_services_um_outcome_t outcome = sdf_services_delete_user(user_id);
    ESP_LOGI(TAG, "Remote delete user_id=%u resolved: %s", (unsigned)user_id,
             sdf_services_um_outcome_name(outcome));
    if (outcome == SDF_SERVICES_UM_OK) {
      led_flash_green();
    } else {
      led_flash_red();
    }
    sdf_services_record_um_result(user_id, 0, outcome);
    /* Falls through to action_cb so sdf_app routes the terminal reply to
     * the requesting BLE connection. */
  }

  if (action == SDF_SERVICES_ADMIN_ACTION_REMOTE_ENROLL) {
    uint16_t user_id = 0;
    uint8_t permission = 0;
    bool start_ok = false;
    {
      SDF_LOCK_GUARD(guard, s_state.lock, SDF_SERVICES_LOCK_WAIT_MS);
      if (guard.acquired != pdTRUE) {
        sdf_services_record_um_result(0, 0, SDF_SERVICES_UM_FAILED);
        if (action_cb != NULL) {
          action_cb(action_ctx, action);
        }
        return;
      }
      user_id = s_state.pending_admin_action_user_id;
      permission = s_state.pending_admin_action_permission;
    }

    /* Between the admitting scan and the start of the enrolment: clear any
     * stale template in the target slot.
     *
     * The sensor and the enrolled-user cache can disagree - an enrolment
     * that captured images but failed before its final store could leave the
     * slot occupied on the sensor while the device still counts it free, and
     * the next store into it would be refused.
     *
     * Kept as a cheap safeguard, not as a fix for anything observed: on this
     * hardware the slot was always already empty (DELETE_USER answers
     * ACK_NOUSER) and the sensor's list matched the cache exactly.
     *
     * Safe because the request was already refused with ID_OCCUPIED if the
     * device believes the slot is taken, so anything still there is by
     * definition a leftover. Outside the services lock, because it is sensor
     * I/O; once per enrolment, never between steps, which would abort the
     * sequence the sensor is running. */
    sdf_fingerprint_op_result_t cleared = fp_delete_user(user_id);
    ESP_LOGI(TAG, "Cleared slot %u before enrolment: %d", (unsigned)user_id,
             (int)cleared);

    {
      SDF_LOCK_GUARD(guard, s_state.lock, SDF_SERVICES_LOCK_WAIT_MS);
      if (guard.acquired != pdTRUE) {
        sdf_services_fp_hold_power(false);
        sdf_services_record_um_result(user_id, permission,
                                      SDF_SERVICES_UM_FAILED);
        if (action_cb != NULL) {
          action_cb(action_ctx, action);
        }
        return;
      }

      /* Only NOW does the enrolment state machine start - a remote
       * enrolment waits for its authorizing scan first (companion-user-mgmt
       * "Remote Enrolment Cannot Bypass The Admin Fingerprint Gate"). The
       * occupied-id guard was checked before arming and is re-checked by
       * the state machine's own start. */
      esp_err_t err =
          sdf_enrollment_sm_start(&s_state.enrollment, user_id, permission);
      if (err == ESP_OK) {
        s_state.enrollment_request_pending = false;
        start_ok = true;
      } else {
        ESP_LOGW(TAG, "Remote enrollment start failed user_id=%u: %s",
                 (unsigned)user_id, esp_err_to_name(err));
      }
    }

    if (!start_ok) {
      led_flash_red();
      sdf_services_fp_hold_power(false);
      sdf_services_record_um_result(user_id, permission, SDF_SERVICES_UM_FAILED);
    } else {
      ESP_LOGI(TAG, "Remote enrollment started user_id=%u permission=%u",
               (unsigned)user_id, (unsigned)permission);
      /* Already held since the scan was armed; idempotent, and covers a
       * flow that reached here by another route. */
      sdf_services_fp_hold_power(true);
      led_pulse_blue();
      sdf_services_emit_enrollment_event(SDF_EVENT_ROUTER_ENROLLMENT_STEP_COMPLETE,
                                         &s_state.enrollment);
      /* STEP_COMPLETE above is for observers (sdf_app, the BLE companion);
       * no one drives the machine from it - the enroll task is not even
       * subscribed to it. Without this kick the enrolment stayed started but
       * idle: the admin scan was accepted and then the new user's finger was
       * never asked for. */
      sdf_enroll_task_run_step_soon();
      sdf_services_record_um_result(user_id, permission, SDF_SERVICES_UM_OK);
    }
    /* Falls through to action_cb so sdf_app routes the terminal reply. */
  }

  if (action == SDF_SERVICES_ADMIN_ACTION_RENAME_USER) {
    uint16_t user_id = 0;
    char name[SDF_STORAGE_WEB_USER_NAME_MAX];
    {
      SDF_LOCK_GUARD(guard, s_state.lock, SDF_SERVICES_LOCK_WAIT_MS);
      if (guard.acquired == pdTRUE) {
        user_id = s_state.pending_admin_action_user_id;
        strncpy(name, s_state.pending_admin_action_name, sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';
      } else {
        name[0] = '\0';
      }
    }

    /* set_user_name() re-applies every guard (enrolled, name uniqueness)
     * at execution time from current state. */
    sdf_services_um_outcome_t outcome =
        sdf_services_set_user_name(user_id, name);
    ESP_LOGI(TAG, "Remote rename user_id=%u resolved: %s", (unsigned)user_id,
             sdf_services_um_outcome_name(outcome));
    if (outcome == SDF_SERVICES_UM_OK) {
      led_flash_green();
    } else {
      led_flash_red();
    }
    sdf_services_record_um_result(user_id, 0, outcome);
    /* Falls through to action_cb so sdf_app routes the terminal reply. */
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
  /* Set when a gated flow ends without an enrolment starting, so the power
   * hold taken at arming does not outlive it. */
  bool release_fp_hold = false;
  bool authorized = false;
  bool um_denied = false;
  bool perm_denied = false;
  SemaphoreHandle_t perm_denied_sem = NULL;

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
      /* Capture the authorizing admin's identity at the moment of the
       * claim (companion-identity): the registration decision binds the
       * persisted credential to this user. Same owned pending-request
       * state as the name/hash - never carried in an event payload. */
      if (action == SDF_SERVICES_ADMIN_ACTION_WEB_REG_AUTH) {
        s_state.request_web_authorizing_user_id = match->user_id;
      }
      authorized = true;
    } else if (has_pending &&
               sdf_services_um_action_is_remote(s_state.pending_admin_action)) {
      /* A non-admin finger cannot leave a remote delete/enroll pending for
       * retry the way other actions can: the requesting BLE client needs
       * its terminal reply, so the action resolves immediately with DENIED
       * (companion-user-mgmt "Pending BLE-Originated Admin Actions Always
       * Resolve"). ADMIN_ACTION_COMPLETE below carries the denial to
       * sdf_app, which pops the recorded outcome. */
      action = s_state.pending_admin_action;
      s_state.pending_admin_action = SDF_SERVICES_ADMIN_ACTION_NONE;
      s_state.pending_admin_action_start_us = 0;
      s_state.um_action_result = SDF_SERVICES_UM_DENIED;
      s_state.um_action_result_user_id = s_state.pending_admin_action_user_id;
      s_state.um_action_result_permission =
          s_state.pending_admin_action_permission;
      s_state.um_action_result_valid = true;
      um_denied = true;
      release_fp_hold = true;
    } else if (has_pending && s_state.pending_admin_action ==
                                  SDF_SERVICES_ADMIN_ACTION_CHANGE_PERMISSION &&
               s_state.permission_change_pending) {
      /* Same reasoning for the permission change, which has a caller
       * blocked on admin_action_done_sem rather than a recorded outcome:
       * resolve it here as denied instead of leaving it to time out, so
       * "the scan was refused" and "no one scanned" stay distinguishable
       * (companion-user-mgmt "Denied and timed-out scans are
       * distinguishable"). The waiter maps ESP_ERR_NOT_ALLOWED to DENIED. */
      action = s_state.pending_admin_action;
      s_state.pending_admin_action = SDF_SERVICES_ADMIN_ACTION_NONE;
      s_state.pending_admin_action_start_us = 0;
      s_state.permission_change_result = ESP_ERR_NOT_ALLOWED;
      s_state.permission_change_pending = false;
      s_state.permission_change_user_id = 0;
      s_state.permission_change_permission = 0;
      perm_denied_sem = s_state.admin_action_done_sem;
      perm_denied = true;
    }
    xSemaphoreGive(s_state.lock);
  }

  if (release_fp_hold) {
    /* Denied: no enrolment will run, so the hold taken at arming ends here
     * rather than leaving the sensor powered until the next reboot. */
    sdf_services_fp_hold_power(false);
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
  } else if (um_denied) {
    ESP_LOGW(TAG,
             "Remote user-management action %d denied by non-admin scan "
             "user_id=%u",
             (int)action, (unsigned)match->user_id);
    led_admin_auth_red();
    sdf_event_router_event_t evt = {
        .type = SDF_EVENT_ROUTER_ADMIN_ACTION_COMPLETE,
        .priority = SDF_EVENT_ROUTER_PRIO_HIGH,
        .timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL),
        .payload.admin_action_complete = {.action = action,
                                          .result = ESP_ERR_INVALID_STATE},
    };
    sdf_event_router_emit(&evt, SDF_EVENT_ROUTER_EMIT_TIMEOUT_DEFAULT_MS);
  } else if (perm_denied) {
    ESP_LOGW(TAG,
             "Permission change denied by non-admin scan user_id=%u",
             (unsigned)match->user_id);
    led_admin_auth_red();
    /* Signalled outside the lock: the waiter re-takes it to read the
     * result it was left. */
    if (perm_denied_sem != NULL) {
      xSemaphoreGive(perm_denied_sem);
    }
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

    /* Initialize task queues idempotently */
    esp_err_t err = sdf_match_task_init_queue();
    if (err != ESP_OK) {
        return err;
    }
    err = sdf_enroll_task_init_queue();
    if (err != ESP_OK) {
        sdf_match_task_deinit_queue();
        return err;
    }
    err = sdf_admin_task_init_queue();
    if (err != ESP_OK) {
        sdf_match_task_deinit_queue();
        sdf_enroll_task_deinit_queue();
        return err;
    }

    /* Create match task */
    BaseType_t task_ok = xTaskCreate(sdf_match_task, SDF_MATCH_TASK_NAME,
                                     SDF_MATCH_TASK_STACK, NULL,
                                     SDF_MATCH_TASK_PRIORITY, &s->match_task);
    if (task_ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create match task");
        sdf_match_task_deinit_queue();
        sdf_enroll_task_deinit_queue();
        sdf_admin_task_deinit_queue();
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
        sdf_match_task_deinit_queue();
        sdf_enroll_task_deinit_queue();
        sdf_admin_task_deinit_queue();
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
        sdf_match_task_deinit_queue();
        sdf_enroll_task_deinit_queue();
        sdf_admin_task_deinit_queue();
        return ESP_FAIL;
    }

    /* Initialize button handling */
    esp_err_t btn_err = sdf_button_init();
    if (btn_err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to initialize button handling: %s", esp_err_to_name(btn_err));
    }

    ESP_LOGI(TAG, "All 3 services tasks started");
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

    sdf_button_deinit();

    if (s->lock != NULL) {
        SDF_LOCK_GUARD(guard, s->lock, SDF_SERVICES_LOCK_WAIT_MS);
        if (guard.acquired == pdTRUE) {
            s->stop_requested = true;
        }
    }

    /* Push stop signal to enroll and admin tasks so they wake immediately */
    sdf_enroll_task_wake();
    sdf_admin_task_wake();

    /* Subscriptions registered by service tasks (match, admin, enroll) are
     * permanent for the lifetime of the boot and cannot be unregistered.
     * If this uncalled shutdown path is ever revived, callbacks will
     * continue to be invoked by the event router but will discard events
     * safely because event_queue is set to NULL on exit.
     *
     * Each task polls stop_requested (see sdf_match_task / sdf_enroll_task /
     * sdf_admin_task) and exits on its own - deinitializing its queue and
     * self-deleting - instead of being killed from outside via vTaskDelete().
     * Killing a task from outside can leave s_state.lock permanently held if
     * the victim happened to be inside a critical section (or, for
     * match/enroll, mid the ~12s blocking fingerprint UART call) at the moment
     * of deletion. Poll for all three to clear their handles, bounded
     * generously enough to cover that worst-case 12s UART timeout. */
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
                          s->enroll_task == NULL && s->admin_task == NULL;
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
    }

    /* Ensure queues are cleaned up in case tasks were force-deleted or
     * did not run their cooperative deinit */
    sdf_match_task_deinit_queue();
    sdf_enroll_task_deinit_queue();
    sdf_admin_task_deinit_queue();

    if (s->lock != NULL) {
        SDF_LOCK_GUARD(guard, s->lock, SDF_SERVICES_LOCK_WAIT_MS);
        if (guard.acquired == pdTRUE) {
            s->stop_requested = false;
        }
    }

    ESP_LOGI(TAG, "All services tasks stopped");
    return ESP_OK;
}


/* sdf_services_init() has to publish initialized=true before it calls
 * sdf_services_start_tasks(), because start_tasks() gates on
 * sdf_services_is_ready(). Every failure path after that point must roll the
 * flag back, or a failed init leaves is_ready() reporting a live module that
 * has no drivers and no tasks. */
static void sdf_services_mark_uninitialized(void) {
  if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(SDF_SERVICES_LOCK_WAIT_MS)) ==
      pdTRUE) {
    s_state.initialized = false;
    xSemaphoreGive(s_state.lock);
  }
}

void sdf_services_restore_lockout_locked(int64_t now_us) {
  /* Called with s_state.lock held and before any task exists (see
   * sdf_services_init). Reads the persisted flag - not a deadline - because
   * esp_timer_get_time() restarts near 0 across power loss (D1). A "not
   * found" record is the normal fresh-device case; any other read failure
   * fails open and is logged so an NVS glitch never bricks biometric entry
   * (D4). Time, when needed, is always a full lockout_duration_ms from
   * boot (D2). NVS read is blocking but this is boot-only and lock-owned
   * by definition; runtime persistence (the writes) stays outside the lock
   * as D3 requires. */
  bool armed = false;
  esp_err_t err = sdf_storage_lockout_load(&armed);
  if (err == ESP_OK && armed) {
    s_state.lockout_until_us = now_us + ((int64_t)s_state.config.lockout_duration_ms * 1000LL);
    s_state.failed_attempt_count = 0;
    s_state.failed_attempt_window_start_us = 0;
    s_state.lockout_persist_armed = true;
    s_state.lockout_restore_announce_pending = true;
  } else if (err == ESP_ERR_NOT_FOUND) {
    /* Never locked out, or cleared - remain not locked out. Fields already
     * zeroed by the caller. */
  } else if (err != ESP_OK) {
    ESP_LOGW(TAG, "Failed to load lockout state, treating as not locked out: %s",
             esp_err_to_name(err));
  } else {
    /* err == ESP_OK && !armed -> cleared record, also not locked out. */
  }
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
  s_state.lockout_persist_armed = false;
  s_state.lockout_restore_announce_pending = false;
  /* Restore any armed lockout from before the reset/power-loss. Reads the
   * persisted flag alongside the enrolled-user cache, still under the same
   * lock and before any task exists so the first scan cannot race it (D4).
   * The call never writes NVS - only the later entry/clear sites do (D3) -
   * and it re-arms a full duration from boot, not a stored deadline (D1/D2). */
  sdf_services_restore_lockout_locked(esp_timer_get_time());
  s_state.pending_admin_action = SDF_SERVICES_ADMIN_ACTION_NONE;
  s_state.pending_admin_action_start_us = 0;
  s_state.permission_change_pending = false;
  s_state.permission_change_user_id = 0;
  s_state.permission_change_permission = 0;
  s_state.permission_change_result = ESP_OK;
  /* Load the persisted enrolled-user cache synchronously, before this
   * function returns and sdf_services_start_tasks() below creates the
   * button/match/admin/enroll tasks - so there is no window after boot
   * where a task can observe the cache under-reporting a claimed device's
   * real admin (see cache-enrolled-user-state). "Key not found" (first boot
   * after this firmware update, or an erased device) is already translated
   * to "zero users, ESP_OK" by sdf_storage_enrolled_users_load(); any other
   * error is logged and conservatively also treated as zero users, since
   * there is no sensor-side fallback query to cross-check against anymore. */
  esp_err_t cache_load_err = sdf_storage_enrolled_users_load(
      &s_state.enrolled_user_bmp, s_state.enrolled_perm_packed);
  if (cache_load_err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to load enrolled-user cache from NVS: %s",
             esp_err_to_name(cache_load_err));
    s_state.enrolled_user_bmp = 0;
    memset(s_state.enrolled_perm_packed, 0, sizeof(s_state.enrolled_perm_packed));
  }
  /* Register event-router subscriptions for service tasks during service init
   * before tasks are created and started. Propagate any registration error. */
  esp_err_t sub_err = sdf_match_task_init_subscriptions();
  if (sub_err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize match subscriptions: %s", esp_err_to_name(sub_err));
    xSemaphoreGive(s_state.lock);
    return sub_err;
  }
  sub_err = sdf_admin_task_init_subscriptions();
  if (sub_err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize admin subscriptions: %s", esp_err_to_name(sub_err));
    xSemaphoreGive(s_state.lock);
    return sub_err;
  }
  sub_err = sdf_enroll_task_init_subscriptions();
  if (sub_err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize enroll subscriptions: %s", esp_err_to_name(sub_err));
    xSemaphoreGive(s_state.lock);
    return sub_err;
  }

  s_state.initialized = true;
  xSemaphoreGive(s_state.lock);

  /* Arm the setup phase on boot when the latch is unset (first boot of an
   * unprovisioned device, or the reboot that follows a factory reset). NVS
   * is up by now, so the latch read is valid. */
  sdf_services_setup_phase_boot_arm();

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
    sdf_services_mark_uninitialized();
    return err;
  }

  /* Start new tasks */
  err = sdf_services_start_tasks();
  if (err != ESP_OK) {
    sdf_drivers_deinit();
    sdf_services_mark_uninitialized();
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

  if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(SDF_SERVICES_LOCK_WAIT_MS)) ==
      pdTRUE) {
    if (s_state.enroll_task != NULL) {
      xTaskNotifyGive(s_state.enroll_task);
    }
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

/* Number of attempts (including the first) sdf_services_persist_enrolled_users_locked()
 * makes before giving up and reporting failure to its caller. */
#define SDF_SERVICES_PERSIST_RETRY_COUNT 3u
#define SDF_SERVICES_PERSIST_RETRY_DELAY_MS 50u

esp_err_t sdf_services_persist_enrolled_users_locked(void) {
  esp_err_t err = ESP_FAIL;
  for (uint32_t attempt = 0; attempt < SDF_SERVICES_PERSIST_RETRY_COUNT; attempt++) {
    err = sdf_storage_enrolled_users_save(s_state.enrolled_user_bmp,
                                          s_state.enrolled_perm_packed);
    if (err == ESP_OK) {
      return ESP_OK;
    }
    ESP_LOGW(TAG,
             "Failed to persist enrolled-user cache to NVS (attempt %u/%u): %s",
             (unsigned)(attempt + 1), (unsigned)SDF_SERVICES_PERSIST_RETRY_COUNT,
             esp_err_to_name(err));
    if (attempt + 1 < SDF_SERVICES_PERSIST_RETRY_COUNT) {
      vTaskDelay(pdMS_TO_TICKS(SDF_SERVICES_PERSIST_RETRY_DELAY_MS));
    }
  }
  return err;
}

sdf_services_um_outcome_t sdf_services_delete_user(uint16_t user_id) {
  if (s_state.lock == NULL)
    return SDF_SERVICES_UM_UNAVAILABLE;

  /* Snapshot the authoritative enrolled-user record before touching the
   * sensor: both guards below are decided from the cached bitmap + packed
   * permissions (cache-enrolled-user-state), the same compact pair and
   * admin-count derivation sdf_services_change_user_permission() decides
   * from. The lock is only held for the copy - see the comment above
   * fp_delete_user() for why it must not span the UART round trip. */
  uint16_t snap_user_bmp = 0;
  uint8_t snap_perm_packed[sizeof(s_state.enrolled_perm_packed)];
  {
    SDF_LOCK_GUARD(guard, s_state.lock, SDF_SERVICES_LOCK_WAIT_MS);
    if (guard.acquired != pdTRUE) {
      return SDF_SERVICES_UM_FAILED;
    }
    snap_user_bmp = s_state.enrolled_user_bmp;
    memcpy(snap_perm_packed, s_state.enrolled_perm_packed,
           sizeof(snap_perm_packed));
  }

  /* A user id that is not set in the snapshot bitmap is not enrolled -
   * report that before any sensor traffic. Ids outside the bitmap's range
   * cannot be set in it either, so they take the same exit. */
  if (user_id < SDF_FINGERPRINT_USER_ID_MIN ||
      user_id > SDF_SERVICES_MAX_USERS ||
      !SDF_SERVICES_BMP_TEST(snap_user_bmp, user_id)) {
    return SDF_SERVICES_UM_NOT_FOUND;
  }

  /* Count admins from the same snapshot - identical derivation to
   * sdf_services_change_user_permission(). */
  size_t admin_count = 0;
  for (uint16_t id = 1; id <= SDF_SERVICES_MAX_USERS; id++) {
    if (SDF_SERVICES_BMP_TEST(snap_user_bmp, id) &&
        sdf_services_perm_get(snap_perm_packed, id) == 3u) {
      admin_count++;
    }
  }

  if (sdf_services_perm_get(snap_perm_packed, user_id) == 3u &&
      admin_count <= 1u) {
    /* Deleting the only admin would permanently strand every
     * admin-fingerprint-gated action (pairing window, Enroll-Admin, Nuki
     * re-pair, Zigbee join, Web Registration Authorization): enrolment of a
     * replacement requires the physical finger, and there is no sensor-side
     * rollback once fp_delete_user() runs. Refuse before the sensor call so
     * a rejected delete is free, under the distinct LAST_ADMIN outcome
     * (companion-user-mgmt) rather than the overloaded INVALID_STATE this
     * guard used to share with "busy". clear_all_users() stays deliberately
     * exempt - losing the last admin is the point of the factory-reset bulk
     * wipe. */
    return SDF_SERVICES_UM_LAST_ADMIN;
  }

  /* fp_delete_user() is a blocking UART round-trip (up to the ~12s sensor
   * timeout) and is already serialized by the fingerprint driver's own
   * internal mutex, so s_state.lock does not need to be held across it -
   * doing so would stall every other s_state.lock user (match cycle, admin
   * actions, enrollment) for up to 12s. Only take the lock for the cache
   * update + synchronous NVS persist below. */
  sdf_fingerprint_op_result_t res = fp_delete_user(user_id);
  if (res != SDF_FINGERPRINT_OP_OK) {
    return SDF_SERVICES_UM_FAILED;
  }

  bool persisted = false;
  {
    SDF_LOCK_GUARD(guard, s_state.lock, SDF_SERVICES_LOCK_WAIT_MS);
    if (guard.acquired == pdTRUE) {
      SDF_SERVICES_BMP_CLEAR(s_state.enrolled_user_bmp, user_id);
      persisted = (sdf_services_persist_enrolled_users_locked() == ESP_OK);
      if (!persisted) {
        /* Leave the cache bit set (stale-but-safe): a deleted-on-sensor,
         * still-cached-as-enrolled user is inert (the sensor will simply
         * never match them again) - no sensor-side rollback per design.md. */
        SDF_SERVICES_BMP_SET(s_state.enrolled_user_bmp, user_id);
      }
    }
  }

  if (!persisted) {
    ESP_LOGE(TAG, "Failed to persist enrolled-user cache after deleting user_id=%u",
             (unsigned)user_id);
    led_flash_red();
    return SDF_SERVICES_UM_FAILED;
  }

  /* Deleting the user destroys their unified record - name and any bound
   * companion credential with it (companion-identity "Deleting A User
   * Destroys Its Companion Account"). The name is released implicitly by
   * the same clear; there is no separate reclamation step. Best-effort: a
   * storage failure here must not fail an already-committed sensor delete. */
  sdf_storage_web_user_clear(user_id);
  return SDF_SERVICES_UM_OK;
}

esp_err_t sdf_services_clear_all_users(void) {
  if (s_state.lock == NULL)
    return ESP_ERR_INVALID_STATE;
  /* See sdf_services_delete_user(): fp_delete_all_users() is a long
   * blocking UART call already serialized by the fingerprint driver's own
   * mutex, so don't hold s_state.lock across it. */
  sdf_fingerprint_op_result_t res = fp_delete_all_users();
  if (res != SDF_FINGERPRINT_OP_OK) {
    return ESP_FAIL;
  }

  bool persisted = false;
  {
    SDF_LOCK_GUARD(guard, s_state.lock, SDF_SERVICES_LOCK_WAIT_MS);
    if (guard.acquired == pdTRUE) {
      uint16_t prev_bmp = s_state.enrolled_user_bmp;
      uint8_t prev_perm[sizeof(s_state.enrolled_perm_packed)];
      memcpy(prev_perm, s_state.enrolled_perm_packed, sizeof(prev_perm));

      s_state.enrolled_user_bmp = 0;
      memset(s_state.enrolled_perm_packed, 0, sizeof(s_state.enrolled_perm_packed));
      persisted = (sdf_services_persist_enrolled_users_locked() == ESP_OK);
      if (!persisted) {
        /* Leave the cache reflecting the last successfully persisted state
         * (stale-but-safe), no sensor-side rollback per design.md. */
        s_state.enrolled_user_bmp = prev_bmp;
        memcpy(s_state.enrolled_perm_packed, prev_perm, sizeof(prev_perm));
      }
    }
  }

  if (!persisted) {
    ESP_LOGE(TAG, "Failed to persist enrolled-user cache after clearing all users");
    led_flash_red();
    return ESP_FAIL;
  }

  /* Clearing all users clears every unified record with it (names and
   * bound credentials alike) - the factory-reset-style bulk wipe. */
  sdf_storage_web_user_clear_all();
  return ESP_OK;
}

esp_err_t sdf_services_query_users(uint16_t *user_ids, uint8_t *permissions,
                                   size_t *count, size_t max_count) {
  if (s_state.lock == NULL)
    return ESP_ERR_INVALID_STATE;

  /* Served entirely from the persisted, in-RAM cache under s_state.lock -
   * no sensor UART round trip (see cache-enrolled-user-state). */
  SDF_LOCK_GUARD(guard, s_state.lock, SDF_SERVICES_LOCK_WAIT_MS);
  if (guard.acquired != pdTRUE) {
    return ESP_ERR_TIMEOUT;
  }

  size_t out_count = 0;
  for (uint16_t id = 1; id <= SDF_SERVICES_MAX_USERS && out_count < max_count; id++) {
    if (SDF_SERVICES_BMP_TEST(s_state.enrolled_user_bmp, id)) {
      user_ids[out_count] = id;
      permissions[out_count] = sdf_services_perm_get(s_state.enrolled_perm_packed, id);
      out_count++;
    }
  }
  *count = out_count;
  return ESP_OK;
}

/* The one user-list serializer (see sdf_services.h). Plain snprintf rather
 * than cJSON so it stays host-testable and allocation-free; the minimal
 * JSON string escaping below keeps names containing quotes, backslashes or
 * control characters valid JSON. */
size_t sdf_services_format_user_list(
    const sdf_services_user_list_entry_t *entries, size_t count, char *buf,
    size_t buf_size) {
  if (buf == NULL || buf_size < 3) {
    return 0;
  }

  size_t pos = 0;
  buf[pos++] = '[';

  for (size_t i = 0; i < count; i++) {
    char entry[SDF_STORAGE_WEB_USER_NAME_MAX * 6 + 32];
    const char *name = entries[i].name;
    bool has_name = name != NULL && name[0] != '\0';

    if (has_name) {
      /* Escape into a scratch big enough for any legal name
       * (SDF_STORAGE_WEB_USER_NAME_MAX caps names well below this). */
      char escaped[SDF_STORAGE_WEB_USER_NAME_MAX * 6 + 1];
      size_t e = 0;
      for (const char *p = name; *p != '\0'; p++) {
        unsigned char c = (unsigned char)*p;
        if (e + 6 >= sizeof(escaped)) {
          break;
        }
        if (c == '"' || c == '\\') {
          escaped[e++] = '\\';
          escaped[e++] = (char)c;
        } else if (c < 0x20) {
          e += (size_t)snprintf(escaped + e, sizeof(escaped) - e, "\\u%04x", c);
        } else {
          escaped[e++] = (char)c;
        }
      }
      escaped[e] = '\0';
      int n = snprintf(entry, sizeof(entry), "%s{\"id\":%u,\"perm\":%u,\"name\":\"%s\"}",
                       i == 0 ? "" : ",", (unsigned)entries[i].id,
                       (unsigned)entries[i].permission, escaped);
      if (n <= 0 || (size_t)n >= sizeof(entry)) {
        return 0;
      }
    } else {
      int n = snprintf(entry, sizeof(entry), "%s{\"id\":%u,\"perm\":%u}",
                       i == 0 ? "" : ",", (unsigned)entries[i].id,
                       (unsigned)entries[i].permission);
      if (n <= 0 || (size_t)n >= sizeof(entry)) {
        return 0;
      }
    }

    size_t len = strlen(entry);
    if (pos + len + 2 > buf_size) {
      return 0;
    }
    memcpy(buf + pos, entry, len);
    pos += len;
  }

  buf[pos++] = ']';
  buf[pos] = '\0';
  return pos;
}

sdf_services_um_outcome_t sdf_services_change_user_permission(uint16_t user_id,
                                                              uint8_t permission) {
  if (user_id < SDF_FINGERPRINT_USER_ID_MIN ||
      user_id > SDF_FINGERPRINT_USER_ID_MAX || permission < 1u ||
      permission > 3u) {
    return SDF_SERVICES_UM_INVALID;
  }

  if (s_state.lock == NULL || s_state.admin_action_done_sem == NULL) {
    return SDF_SERVICES_UM_UNAVAILABLE;
  }

  bool initialized = false;
  {
    SDF_LOCK_GUARD(guard, s_state.lock, SDF_SERVICES_LOCK_WAIT_MS);
    if (guard.acquired != pdTRUE) {
      return SDF_SERVICES_UM_FAILED;
    }

    initialized = s_state.initialized;
    if (!initialized || s_state.pending_admin_action != SDF_SERVICES_ADMIN_ACTION_NONE ||
        s_state.permission_change_pending ||
        s_state.enrollment_request_pending ||
        sdf_enrollment_sm_is_active(&s_state.enrollment)) {
      return s_state.initialized ? SDF_SERVICES_UM_BUSY
                                 : SDF_SERVICES_UM_UNAVAILABLE;
    }
  }

  if (!initialized) {
    return SDF_SERVICES_UM_UNAVAILABLE;
  }

  const size_t query_capacity = (size_t)SDF_SERVICES_MAX_USERS;
  size_t count = query_capacity;
  esp_err_t err;

  if (esp_get_free_heap_size() < 4096) {
    ESP_LOGE(TAG, "Insufficient heap for permission change query");
    sdf_app_emit_audit(SDF_AUDIT_PROTOCOL_ERROR, 0, ESP_ERR_NO_MEM, 0);
    return SDF_SERVICES_UM_FAILED;
  }

  /* Use local temp arrays for query, then pack into compact format */
  uint16_t user_ids[SDF_SERVICES_MAX_USERS];
  uint8_t perms[SDF_SERVICES_MAX_USERS];

  uint16_t perm_user_bmp = 0;
  uint8_t perm_perm_packed[SDF_SERVICES_PERM_PACKED_SIZE] = {0};

  err = sdf_services_query_users(user_ids, perms, &count, query_capacity);
  if (err != ESP_OK) {
    return SDF_SERVICES_UM_FAILED;
  }

  /* Pack results into compact bitmap + packed permissions format */
  sdf_services_pack_user_list(user_ids, perms, count,
                              &perm_user_bmp,
                              perm_perm_packed);

  bool found = SDF_SERVICES_BMP_TEST(perm_user_bmp, user_id);
  uint8_t current_permission = found ? sdf_services_perm_get(perm_perm_packed, user_id) : 0;

  /* Count admins using packed permissions */
  size_t admin_count = 0;
  for (uint16_t id = 1; id <= SDF_SERVICES_MAX_USERS; id++) {
    if (SDF_SERVICES_BMP_TEST(perm_user_bmp, id) &&
        sdf_services_perm_get(perm_perm_packed, id) == 3u) {
      admin_count++;
    }
  }

  if (!found) {
    return SDF_SERVICES_UM_NOT_FOUND;
  }

  if (current_permission == permission) {
    return SDF_SERVICES_UM_OK;
  }

  if (current_permission == 3u && permission != 3u && admin_count <= 1u) {
    return SDF_SERVICES_UM_LAST_ADMIN;
  }

  {
    SDF_LOCK_GUARD(guard, s_state.lock, SDF_SERVICES_LOCK_WAIT_MS);
    if (guard.acquired != pdTRUE) {
      return SDF_SERVICES_UM_FAILED;
    }

    if (s_state.pending_admin_action != SDF_SERVICES_ADMIN_ACTION_NONE ||
        s_state.permission_change_pending ||
        s_state.enrollment_request_pending ||
        sdf_enrollment_sm_is_active(&s_state.enrollment)) {
      return SDF_SERVICES_UM_BUSY;
    }

    while (xSemaphoreTake(s_state.admin_action_done_sem, 0) == pdTRUE) {
    }

    s_state.permission_change_pending = true;
    s_state.permission_change_user_id = user_id;
    s_state.permission_change_permission = permission;
    s_state.permission_change_result = ESP_ERR_TIMEOUT;
    s_state.pending_admin_action = SDF_SERVICES_ADMIN_ACTION_CHANGE_PERMISSION;
    s_state.pending_admin_action_start_us = esp_timer_get_time();
    sdf_admin_task_wake();
  }

  led_pulse_blue();
  if (s_state.wake_sem != NULL) {
    xSemaphoreGive(s_state.wake_sem);
  }

  bool resolved = false;
  for (uint32_t waited_ms = 0;
       waited_ms < SDF_SERVICES_PERMISSION_CHANGE_WAIT_MS;
       waited_ms += SDF_SERVICES_PERMISSION_CHANGE_SLICE_MS) {
    sdf_platform_time_wdt_reset();
    if (xSemaphoreTake(s_state.admin_action_done_sem,
                       pdMS_TO_TICKS(SDF_SERVICES_PERMISSION_CHANGE_SLICE_MS)) ==
        pdTRUE) {
      resolved = true;
      break;
    }
  }
  sdf_platform_time_wdt_reset();

  if (!resolved) {
    return SDF_SERVICES_UM_TIMEOUT;
  }

  {
    SDF_LOCK_GUARD(guard, s_state.lock, SDF_SERVICES_LOCK_WAIT_MS);
    if (guard.acquired != pdTRUE) {
      return SDF_SERVICES_UM_FAILED;
    }
    switch (s_state.permission_change_result) {
    case ESP_OK:
      return SDF_SERVICES_UM_OK;
    case ESP_ERR_TIMEOUT:
      return SDF_SERVICES_UM_TIMEOUT;
    case ESP_ERR_NOT_ALLOWED:
      /* The gate resolved the action against a non-admin scan. */
      return SDF_SERVICES_UM_DENIED;
    default:
      return SDF_SERVICES_UM_FAILED;
    }
  }
}

bool sdf_services_user_is_enrolled(uint16_t user_id) {
  if (user_id < SDF_FINGERPRINT_USER_ID_MIN ||
      user_id > SDF_SERVICES_MAX_USERS || s_state.lock == NULL) {
    return false;
  }

  bool enrolled = false;
  SDF_LOCK_GUARD(guard, s_state.lock, SDF_SERVICES_LOCK_WAIT_MS);
  if (guard.acquired == pdTRUE) {
    enrolled = SDF_SERVICES_BMP_TEST(s_state.enrolled_user_bmp, user_id);
  }
  return enrolled;
}

bool sdf_services_user_is_enrolled_admin(uint16_t user_id) {
  if (user_id < SDF_FINGERPRINT_USER_ID_MIN ||
      user_id > SDF_SERVICES_MAX_USERS || s_state.lock == NULL) {
    return false;
  }

  bool is_admin = false;
  SDF_LOCK_GUARD(guard, s_state.lock, SDF_SERVICES_LOCK_WAIT_MS);
  if (guard.acquired == pdTRUE) {
    is_admin = SDF_SERVICES_BMP_TEST(s_state.enrolled_user_bmp, user_id) &&
               sdf_services_perm_get(s_state.enrolled_perm_packed, user_id) == 3u;
  }
  return is_admin;
}

esp_err_t sdf_services_find_name_holder(const char *name, uint16_t *holder_id_out) {
  if (name == NULL || name[0] == '\0' || holder_id_out == NULL ||
      s_state.lock == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  /* Scan every unified record - including name-only records without a
   * credential - because uniqueness spans all enrolled users, not just
   * account holders (companion-identity "The User's Name Is The Login
   * Identifier"). Ten records max, so the linear walk is bounded. */
  for (uint16_t id = SDF_FINGERPRINT_USER_ID_MIN; id <= SDF_SERVICES_MAX_USERS; id++) {
    sdf_storage_web_user_t rec;
    esp_err_t err = sdf_storage_web_user_load(id, &rec);
    if (err == ESP_OK && rec.valid && strcmp(rec.name, name) == 0) {
      *holder_id_out = id;
      return ESP_OK;
    }
  }
  return ESP_ERR_NOT_FOUND;
}

sdf_services_um_outcome_t sdf_services_set_user_name(uint16_t user_id, const char *name) {
  if (user_id < SDF_FINGERPRINT_USER_ID_MIN || user_id > SDF_SERVICES_MAX_USERS ||
      name == NULL || name[0] == '\0' ||
      strlen(name) >= SDF_STORAGE_WEB_USER_NAME_MAX) {
    return SDF_SERVICES_UM_INVALID;
  }

  if (s_state.lock == NULL) {
    return SDF_SERVICES_UM_UNAVAILABLE;
  }

  /* Enrolled check from the authoritative cache, same as every other
   * user-scoped operation (cache-enrolled-user-state). */
  bool enrolled = false;
  {
    SDF_LOCK_GUARD(guard, s_state.lock, SDF_SERVICES_LOCK_WAIT_MS);
    if (guard.acquired != pdTRUE) {
      return SDF_SERVICES_UM_FAILED;
    }
    enrolled = SDF_SERVICES_BMP_TEST(s_state.enrolled_user_bmp, user_id);
  }
  if (!enrolled) {
    return SDF_SERVICES_UM_NOT_FOUND;
  }

  /* Uniqueness: refuse a rename onto a name another enrolled user already
   * holds, leaving both records unchanged. Renaming to the user's own
   * current name falls through as a legitimate no-op-equivalent write. */
  uint16_t holder_id = 0;
  if (sdf_services_find_name_holder(name, &holder_id) == ESP_OK &&
      holder_id != user_id) {
    return SDF_SERVICES_UM_NAME_TAKEN;
  }

  /* Merge into the existing record so any stored credential survives the
   * rename; an absent key (never named before) starts a fresh record. */
  sdf_storage_web_user_t rec = {0};
  esp_err_t err = sdf_storage_web_user_load(user_id, &rec);
  if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
    return SDF_SERVICES_UM_FAILED;
  }

  strncpy(rec.name, name, SDF_STORAGE_WEB_USER_NAME_MAX - 1);
  rec.name[SDF_STORAGE_WEB_USER_NAME_MAX - 1] = '\0';
  /* A written name means a record exists; has_credential keeps whatever the
   * previous record said (name-only records stay name-only). */
  rec.valid = true;
  return sdf_storage_web_user_save(user_id, &rec) == ESP_OK
             ? SDF_SERVICES_UM_OK
             : SDF_SERVICES_UM_FAILED;
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

    sdf_services_pulse_pending_action_led(action);
    sdf_admin_task_wake();
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
  /* Both existing callers (sdf_app.c's FACTORY_RESET admin action and the
   * "factory reset" CLI command) run this as one step of a sequence that
   * already calls sdf_storage_erase_all() (wiping the persisted enrolled-
   * user NVS key, among everything else) before reaching this function, so
   * there's nothing left to separately persist here - just zero the in-RAM
   * cache to match the already-erased NVS/sensor state, consistent with
   * every other field reset below. */
  s_state.enrolled_user_bmp = 0;
  memset(s_state.enrolled_perm_packed, 0, sizeof(s_state.enrolled_perm_packed));
  s_state.failed_attempt_count = 0;
  s_state.lockout_until_us = 0;
  s_state.failed_attempt_window_start_us = 0;
  s_state.lockout_persist_armed = false;
  s_state.lockout_restore_announce_pending = false;
  s_state.pending_admin_action = SDF_SERVICES_ADMIN_ACTION_NONE;
  s_state.pending_admin_action_start_us = 0;
  s_state.pending_admin_action_user_id = 0;
  s_state.pending_admin_action_permission = 0;
  s_state.pending_admin_action_name[0] = '\0';
  s_state.um_action_result = SDF_SERVICES_UM_OK;
  s_state.um_action_result_user_id = 0;
  s_state.um_action_result_permission = 0;
  s_state.um_action_result_valid = false;
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

/* Zeros the in-RAM enrolled-user cache AND persists the zeroed record to
 * NVS. Unlike sdf_services_reset_state() (whose callers erase all of NVS
 * first), the setup-phase timeout wipe is selective - it must not nuke
 * unrelated keys like the web pseudo-salt - so the enrolled-user record
 * needs its own explicit persistence here. */
void sdf_services_reset_enrolled_user_cache(void) {
  if (s_state.lock == NULL) {
    return;
  }

  if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(SDF_SERVICES_LOCK_WAIT_MS)) != pdTRUE) {
    return;
  }

  s_state.enrolled_user_bmp = 0;
  memset(s_state.enrolled_perm_packed, 0, sizeof(s_state.enrolled_perm_packed));
  esp_err_t err = sdf_services_persist_enrolled_users_locked();

  xSemaphoreGive(s_state.lock);

  if (err != ESP_OK) {
    /* Non-fatal: the timeout wipe continues; a stale enrolled-user cache
     * key cannot outlive the next factory reset, and the sensor templates
     * themselves were already erased above. */
    ESP_LOGW(TAG, "Failed to persist zeroed enrolled-user cache: %s",
             esp_err_to_name(err));
  }
}

/* Holds the fingerprint sensor powered for the duration of an enrolment
 * flow, and releases it when the flow ends.
 *
 * Default behaviour is per-operation: every bracketed access powers the
 * sensor up and back down, which is what a wake-driven match and a user-list
 * read want. An enrolment cannot work that way - it is three scans plus the
 * authorizing admin scan, and the sensor answers nothing for a while after
 * each power cycle, so a command issued between them times out or reads the
 * line's own echo back.
 *
 * The hold is therefore taken when a flow BEGINS - for the gated path that
 * means when the admin scan is armed, not after it succeeds, so the scan and
 * the enrolment share one power-on - and released on every terminal path:
 * completion after the third scan stores, failure, gate denial, and gate
 * timeout. Call outside s_state.lock: this routes through the fp owner
 * task's queue. */
void sdf_services_fp_hold_power(bool hold) {
  esp_err_t err = fp_set_keep_power_on(hold);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Fingerprint power %s failed: %s", hold ? "hold" : "release",
             esp_err_to_name(err));
  }
}

sdf_services_um_outcome_t sdf_services_request_enrollment(uint16_t user_id,
                                                          uint8_t permission) {
  if (user_id < SDF_FINGERPRINT_USER_ID_MIN ||
      user_id > SDF_FINGERPRINT_USER_ID_MAX || permission < 1u ||
      permission > 3u) {
    return SDF_SERVICES_UM_INVALID;
  }

  if (s_state.lock == NULL) {
    return SDF_SERVICES_UM_UNAVAILABLE;
  }

  if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(SDF_SERVICES_LOCK_WAIT_MS)) != pdTRUE) {
    return SDF_SERVICES_UM_FAILED;
  }

  if (!s_state.initialized) {
    xSemaphoreGive(s_state.lock);
    return SDF_SERVICES_UM_UNAVAILABLE;
  }

  /* Occupied-id check, performed here so every caller gets it - the CLI
   * used to run its own query loop for exactly this and other callers got
   * nothing (companion-user-mgmt). */
  if (SDF_SERVICES_BMP_TEST(s_state.enrolled_user_bmp, user_id)) {
    xSemaphoreGive(s_state.lock);
    return SDF_SERVICES_UM_ID_OCCUPIED;
  }

  if (s_state.enrollment_request_pending ||
      sdf_enrollment_sm_is_active(&s_state.enrollment)) {
    xSemaphoreGive(s_state.lock);
    return SDF_SERVICES_UM_BUSY;
  }

  s_state.request_user_id = user_id;
  s_state.request_permission = permission;
  s_state.enrollment_request_pending = true;
  xSemaphoreGive(s_state.lock);

  /* Powered for the whole flow, released on its terminal paths. */
  sdf_services_fp_hold_power(true);

  /* Hand the request to sdf_enroll_task, which owns the state machine and
   * the sensor. It picks work up ONLY from its event queue - setting
   * enrollment_request_pending is not a signal it ever sees, and the
   * wake_sem given below is taken by nobody. Without this emit the caller
   * got SDF_SERVICES_UM_OK, the companion showed "place your finger", and
   * no command was ever sent to the sensor. */
  sdf_event_router_event_t evt = {
      .type = SDF_EVENT_ROUTER_ENROLLMENT_START,
      .timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL),
      .priority = SDF_EVENT_ROUTER_PRIO_HIGH,
      .payload.enrollment_start = {.action = permission, .user_id = user_id},
  };
  if (sdf_event_router_emit(&evt, SDF_EVENT_ROUTER_EMIT_TIMEOUT_DEFAULT_MS) != ESP_OK) {
    /* Nothing will start the enrolment, so do not report OK: clear the
     * pending flag and let the caller name the failure. */
    if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(SDF_SERVICES_LOCK_WAIT_MS)) ==
        pdTRUE) {
      s_state.enrollment_request_pending = false;
      xSemaphoreGive(s_state.lock);
    }
    ESP_LOGE(TAG, "Failed to queue enrollment start for user_id=%u",
             (unsigned)user_id);
    sdf_services_fp_hold_power(false);
    return SDF_SERVICES_UM_FAILED;
  }

  if (s_state.wake_sem != NULL) {
    xSemaphoreGive(s_state.wake_sem);
  }
  return SDF_SERVICES_UM_OK;
}

sdf_services_um_outcome_t sdf_services_request_delete_user(uint16_t user_id) {
  if (s_state.lock == NULL) {
    return SDF_SERVICES_UM_UNAVAILABLE;
  }

  sdf_services_um_outcome_t outcome = SDF_SERVICES_UM_OK;
  if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(SDF_SERVICES_LOCK_WAIT_MS)) != pdTRUE) {
    return SDF_SERVICES_UM_FAILED;
  }

  /* Guards before the gate: a deletion that can never be permitted does
   * not arm anything, pulse the pending-action LED, or ask anyone to scan
   * (companion-user-mgmt "User Deletion Is An Admin-Fingerprint-Gated
   * Action"). */
  outcome = sdf_services_um_remote_guards_locked(
      s_state.initialized, user_id, 0, false);
  if (outcome != SDF_SERVICES_UM_OK) {
    xSemaphoreGive(s_state.lock);
    return outcome;
  }

  s_state.pending_admin_action = SDF_SERVICES_ADMIN_ACTION_DELETE_USER;
  s_state.pending_admin_action_user_id = user_id;
  s_state.pending_admin_action_permission = 0;
  s_state.pending_admin_action_start_us = esp_timer_get_time();
  sdf_services_pulse_pending_action_led(s_state.pending_admin_action);
  sdf_admin_task_wake();
  xSemaphoreGive(s_state.lock);

  if (s_state.wake_sem != NULL) {
    xSemaphoreGive(s_state.wake_sem);
  }
  return SDF_SERVICES_UM_OK;
}

sdf_services_um_outcome_t sdf_services_request_remote_enrollment(
    uint16_t user_id, uint8_t permission) {
  if (s_state.lock == NULL) {
    return SDF_SERVICES_UM_UNAVAILABLE;
  }

  sdf_services_um_outcome_t outcome = SDF_SERVICES_UM_OK;
  if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(SDF_SERVICES_LOCK_WAIT_MS)) != pdTRUE) {
    return SDF_SERVICES_UM_FAILED;
  }

  /* Guards before the gate, including ID_OCCUPIED: a remote enrolment that
   * cannot proceed never arms the gate. The enrolment state machine itself
   * is NOT started here - only an authorizing admin scan claiming
   * REMOTE_ENROLL starts it (see execute_admin_action()). */
  outcome = sdf_services_um_remote_guards_locked(
      s_state.initialized, user_id, permission, true);
  if (outcome != SDF_SERVICES_UM_OK) {
    xSemaphoreGive(s_state.lock);
    return outcome;
  }

  s_state.pending_admin_action = SDF_SERVICES_ADMIN_ACTION_REMOTE_ENROLL;
  s_state.pending_admin_action_user_id = user_id;
  s_state.pending_admin_action_permission = permission;
  s_state.pending_admin_action_start_us = esp_timer_get_time();
  sdf_services_pulse_pending_action_led(s_state.pending_admin_action);
  sdf_admin_task_wake();
  xSemaphoreGive(s_state.lock);

  /* Held from here, so the authorizing scan and the three enrolment scans
   * share ONE power-on. Cycling power between them is what broke enrolment:
   * the sensor does not answer for some time after a cycle, and ENROLL_1
   * issued a second later just times out, while the same command on an
   * already-powered sensor returns ACK_SUCCESS.
   *
   * fp_set_keep_power_on() waits on the fp owner task, so this only became
   * safe once idle match polls stopped occupying that task for their full
   * 12 s timeout (FP_MATCH_POLL_TIMEOUT_MS): acquiring here used to stall
   * for ~10 s and land after the gate had already expired.
   *
   * Released on every terminal path - enrolment complete or failed, gate
   * denied, gate timed out - so an unanswered gate cannot leave the sensor
   * powered. */
  sdf_services_fp_hold_power(true);

  if (s_state.wake_sem != NULL) {
    xSemaphoreGive(s_state.wake_sem);
  }
  return SDF_SERVICES_UM_OK;
}

sdf_services_um_outcome_t sdf_services_request_rename_user(uint16_t user_id,
                                                           const char *name) {
  if (name == NULL || name[0] == '\0' ||
      strlen(name) >= SDF_STORAGE_WEB_USER_NAME_MAX) {
    return SDF_SERVICES_UM_INVALID;
  }

  if (s_state.lock == NULL) {
    return SDF_SERVICES_UM_UNAVAILABLE;
  }

  if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(SDF_SERVICES_LOCK_WAIT_MS)) != pdTRUE) {
    return SDF_SERVICES_UM_FAILED;
  }

  if (!s_state.initialized) {
    xSemaphoreGive(s_state.lock);
    return SDF_SERVICES_UM_UNAVAILABLE;
  }

  if (user_id < SDF_FINGERPRINT_USER_ID_MIN ||
      user_id > SDF_SERVICES_MAX_USERS) {
    xSemaphoreGive(s_state.lock);
    return SDF_SERVICES_UM_INVALID;
  }

  if (s_state.pending_admin_action != SDF_SERVICES_ADMIN_ACTION_NONE ||
      s_state.permission_change_pending ||
      s_state.enrollment_request_pending ||
      sdf_enrollment_sm_is_active(&s_state.enrollment)) {
    xSemaphoreGive(s_state.lock);
    return SDF_SERVICES_UM_BUSY;
  }

  if (!SDF_SERVICES_BMP_TEST(s_state.enrolled_user_bmp, user_id)) {
    xSemaphoreGive(s_state.lock);
    return SDF_SERVICES_UM_NOT_FOUND;
  }

  /* Name-uniqueness guard before the gate: a rename that can never be
   * permitted does not ask anyone to scan. */
  uint16_t holder_id = 0;
  if (sdf_services_find_name_holder(name, &holder_id) == ESP_OK &&
      holder_id != user_id) {
    xSemaphoreGive(s_state.lock);
    return SDF_SERVICES_UM_NAME_TAKEN;
  }

  s_state.pending_admin_action = SDF_SERVICES_ADMIN_ACTION_RENAME_USER;
  s_state.pending_admin_action_user_id = user_id;
  s_state.pending_admin_action_permission = 0;
  strncpy(s_state.pending_admin_action_name, name,
          sizeof(s_state.pending_admin_action_name) - 1);
  s_state.pending_admin_action_name[sizeof(s_state.pending_admin_action_name) - 1] = '\0';
  s_state.pending_admin_action_start_us = esp_timer_get_time();
  sdf_services_pulse_pending_action_led(s_state.pending_admin_action);
  sdf_admin_task_wake();
  xSemaphoreGive(s_state.lock);

  if (s_state.wake_sem != NULL) {
    xSemaphoreGive(s_state.wake_sem);
  }
  return SDF_SERVICES_UM_OK;
}

bool sdf_services_take_um_action_result(uint16_t *user_id_out,
                                        uint8_t *permission_out,
                                        sdf_services_um_outcome_t *outcome_out) {
  if (user_id_out == NULL || permission_out == NULL || outcome_out == NULL ||
      s_state.lock == NULL) {
    return false;
  }

  bool valid = false;
  if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(SDF_SERVICES_LOCK_WAIT_MS)) == pdTRUE) {
    if (s_state.um_action_result_valid) {
      *user_id_out = s_state.um_action_result_user_id;
      *permission_out = s_state.um_action_result_permission;
      *outcome_out = s_state.um_action_result;
      s_state.um_action_result_valid = false;
      s_state.um_action_result = SDF_SERVICES_UM_OK;
      valid = true;
    }
    xSemaphoreGive(s_state.lock);
  }
  return valid;
}

sdf_services_setup_state_t sdf_services_get_setup_state(void) {
  /* Completion is latched: written once at explicit completion and cleared
   * only by factory reset. Derived state (enrolled users, Nuki credentials)
   * can be mutated by unrelated operations at any time and must never
   * reopen the setup phase on a device in service. */
  bool complete = false;
  if (sdf_storage_setup_complete_load(&complete) == ESP_OK && complete) {
    return SDF_SERVICES_SETUP_STATE_COMPLETE;
  }

  size_t enrolled_user_count = 0;

  if (s_state.lock != NULL) {
    SDF_LOCK_GUARD(guard, s_state.lock, SDF_SERVICES_LOCK_WAIT_MS);
    if (guard.acquired == pdTRUE) {
      enrolled_user_count = sdf_services_enrolled_user_count(s_state.enrolled_user_bmp);
    }
  }

  if (enrolled_user_count == 0) {
    return SDF_SERVICES_SETUP_STATE_NOT_STARTED;
  }

  /* Registration sits between Admin enrolment and Nuki pairing: the bridge
   * authorizes it with an Admin fingerprint scan, so it cannot precede
   * enrolment. Reporting it as its own state keeps the wizard from resuming
   * at a step the user already finished, and lets the completion check
   * require the terminal pre-completion state instead of re-deriving each
   * prerequisite (a device must never end up claimed with no account). */
  size_t web_user_count = 0;
  if (sdf_storage_web_user_count(&web_user_count) != ESP_OK ||
      web_user_count == 0) {
    return SDF_SERVICES_SETUP_STATE_ADMIN_ENROLLED;
  }

  uint32_t authorization_id = 0;
  uint8_t shared_key[32] = {0};
  if (sdf_storage_nuki_load(&authorization_id, shared_key) == ESP_OK) {
    return SDF_SERVICES_SETUP_STATE_NUKI_PAIRED;
  }
  return SDF_SERVICES_SETUP_STATE_REGISTERED;
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
    /* Idempotent: both entry points hold power before reaching here, and a
     * button-driven start that arrives by another route still needs it. */
    sdf_services_fp_hold_power(true);
    led_pulse_blue();
    sdf_services_emit_enrollment_event(SDF_EVENT_ROUTER_ENROLLMENT_STEP_COMPLETE,
                                       &s_state.enrollment);
    return;
  }

  if (failed) {
    sdf_services_fp_hold_power(false);
    sdf_services_emit_enrollment_event(SDF_EVENT_ROUTER_ENROLLMENT_STEP_COMPLETE,
                                       &s_state.enrollment);
  }
}

esp_err_t sdf_services_get_web_reg_auth(char *username, size_t username_max,
                                         uint16_t *authorizing_user_id) {
  if (!username || !authorizing_user_id) {
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
  *authorizing_user_id = s_state.request_web_authorizing_user_id;

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
    s_state.request_web_authorizing_user_id = 0;
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
  s_state.request_web_authorizing_user_id = 0;
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
