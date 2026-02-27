#include "sdf_app.h"

#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#ifndef CONFIG_IDF_TARGET_LINUX
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#endif
#include "mbedtls/platform_util.h"
#include "sdkconfig.h"

#include "sdf_lock_flow.h"
#ifndef CONFIG_IDF_TARGET_LINUX
#include "sdf_nuki_ble_transport.h"
#include "sdf_nuki_pairing.h"
#else
#include "sdf_app_mock_linux.inc"
#endif
#include "sdf_drivers.h"
#include "sdf_power.h"
#include "sdf_protocol_ble.h"
#include "sdf_protocol_zigbee.h"
#include "sdf_services.h"
#include "sdf_storage.h"

#define SDF_APP_ID 1u
#define SDF_APP_NAME "SDF"
#define SDF_APP_LOCK_SUFFIX "SDF"
#define SDF_APP_LOCK_ACTION_MAX_RETRIES 2u
#define SDF_APP_ZB_ALARM_ACTION_FAILURE 0x0001u
#define SDF_APP_ZB_ALARM_LOW_BATTERY 0x0002u
#define SDF_APP_ZB_ALARM_BIOMETRIC_LOCKOUT 0x0004u
#define SDF_APP_ZB_ALARM_SECURITY_PROTOCOL 0x0008u

#define SDF_APP_FP_UART_PORT 1
#define SDF_APP_FP_UART_TX_PIN 4
#define SDF_APP_FP_UART_RX_PIN 5
#define SDF_APP_FP_MATCH_POLL_MS 400u
#define SDF_APP_FP_MATCH_COOLDOWN_MS 3000u
#define SDF_APP_FP_WAKE_GPIO ((gpio_num_t)CONFIG_SDF_POWER_FP_WAKE_GPIO)
#define SDF_APP_FP_POWER_EN_GPIO ((gpio_num_t)CONFIG_SDF_POWER_FP_EN_GPIO)
#define SDF_APP_ENROLLMENT_BTN_GPIO ((gpio_num_t)CONFIG_SDF_ENROLLMENT_BTN_GPIO)
#define SDF_APP_WS2812_LED_GPIO ((gpio_num_t)CONFIG_SDF_WS2812_LED_GPIO)
#define SDF_APP_BATTERY_ADC_GPIO 0

#define SDF_APP_POWER_CHECKIN_INTERVAL_MS                                      \
  ((uint32_t)CONFIG_SDF_POWER_CHECKIN_INTERVAL_MS)
#define SDF_APP_TWDT_TIMEOUT_MS 15000u
#define SDF_APP_POWER_IDLE_BEFORE_SLEEP_MS                                     \
  ((uint32_t)CONFIG_SDF_POWER_IDLE_BEFORE_SLEEP_MS)
#define SDF_APP_POWER_POST_WAKE_GUARD_MS                                       \
  ((uint32_t)CONFIG_SDF_POWER_POST_WAKE_GUARD_MS)
#define SDF_APP_POWER_LOOP_INTERVAL_MS                                         \
  ((uint32_t)CONFIG_SDF_POWER_LOOP_INTERVAL_MS)
#define SDF_APP_POWER_BATTERY_REPORT_MS                                        \
  ((uint32_t)CONFIG_SDF_POWER_BATTERY_REPORT_INTERVAL_MS)
#define SDF_APP_POWER_BATTERY_DEFAULT_PERCENT                                  \
  ((uint8_t)CONFIG_SDF_POWER_BATTERY_DEFAULT_PERCENT)
#define SDF_APP_POWER_ENABLE_LIGHT_SLEEP CONFIG_SDF_POWER_ENABLE_LIGHT_SLEEP
#define SDF_APP_POWER_ENABLE_BLE_RADIO_GATING                                  \
  CONFIG_SDF_POWER_ENABLE_BLE_RADIO_GATING
// Set this to your lock's BLE address to avoid accidental pairing.
// Example for lock address AA:BB:CC:DD:EE:FF (LSB first for NimBLE):
// {0xFF, 0xEE, 0xDD, 0xCC, 0xBB, 0xAA}
#define SDF_NUKI_TARGET_ADDR_TYPE BLE_ADDR_RANDOM
static const ble_addr_t SDF_NUKI_TARGET_ADDR = {
    .type = SDF_NUKI_TARGET_ADDR_TYPE,
    .val = {0xFF, 0xEE, 0xDD, 0xCC, 0xBB, 0xAA}};

static const char *TAG = "sdf_app";

static sdf_nuki_ble_transport_t s_ble;
static sdf_nuki_client_t s_client;
static sdf_nuki_pairing_t s_pairing;
static sdf_event_cb s_event_cb;
static void *s_event_ctx;
static sdf_audit_cb s_audit_cb;
static void *s_audit_ctx;

// Diagnostic counters
static uint32_t s_app_audit_err_biometric_failed = 0;
static uint32_t s_app_audit_err_auth_lockout = 0;
static uint32_t s_app_audit_err_nonce_replay = 0;
static uint32_t s_app_audit_err_protocol = 0;
static bool s_has_creds;
static bool s_pairing_active;
static uint16_t s_zigbee_alarm_mask;
static bool s_latch_sequence_active;

static sdf_lock_flow_t s_lock_flow;

static void sdf_app_emit_audit(sdf_audit_event_type_t type, uint16_t user_id,
                               int32_t status, uint16_t detail);

static const char *sdf_app_status_name(uint8_t status) {
  switch (status) {
  case SDF_STATUS_ACCEPTED:
    return "ACCEPTED";
  case SDF_STATUS_COMPLETE:
    return "COMPLETE";
  default:
    return "UNKNOWN";
  }
}

static void sdf_app_set_alarm_mask_bits(uint16_t set_bits,
                                        uint16_t clear_bits) {
  s_zigbee_alarm_mask |= set_bits;
  s_zigbee_alarm_mask &= (uint16_t)(~clear_bits);
  sdf_protocol_zigbee_update_alarm_mask(s_zigbee_alarm_mask);
}

static void sdf_app_abort_latch_sequence(const char *reason) {
  if (!s_latch_sequence_active) {
    return;
  }

  s_latch_sequence_active = false;
  ESP_LOGW(TAG, "Aborted latch sequence: %s", reason);
}

static sdf_protocol_zigbee_lock_state_t
sdf_app_map_lock_state_to_zigbee(uint8_t nuki_lock_state) {
  switch (nuki_lock_state) {
  case 0x01: /* locked */
    return SDF_PROTOCOL_ZIGBEE_LOCK_STATE_LOCKED;
  case 0x03: /* unlocked */
  case 0x05: /* unlatched */
  case 0x06: /* unlocked (lock n go) */
    return SDF_PROTOCOL_ZIGBEE_LOCK_STATE_UNLOCKED;
  case 0x02: /* unlocking */
  case 0x04: /* locking */
  case 0x07: /* unlatching */
    return SDF_PROTOCOL_ZIGBEE_LOCK_STATE_NOT_FULLY_LOCKED;
  default:
    return SDF_PROTOCOL_ZIGBEE_LOCK_STATE_UNDEFINED;
  }
}

static void sdf_app_update_zigbee_from_action(uint8_t lock_action) {
  switch (lock_action) {
  case SDF_LOCK_ACTION_LOCK:
  case SDF_LOCK_ACTION_LOCK_N_GO:
  case SDF_LOCK_ACTION_FULL_LOCK:
    sdf_protocol_zigbee_update_lock_state(
        SDF_PROTOCOL_ZIGBEE_LOCK_STATE_LOCKED);
    break;
  case SDF_LOCK_ACTION_UNLOCK:
  case SDF_LOCK_ACTION_UNLATCH:
    sdf_protocol_zigbee_update_lock_state(
        SDF_PROTOCOL_ZIGBEE_LOCK_STATE_UNLOCKED);
    break;
  default:
    break;
  }
}

static const char *sdf_app_zb_programming_cmd_name(uint8_t cmd_id) {
  switch (cmd_id) {
  case 0x05:
    return "SET_PIN_CODE";
  case 0x07:
    return "CLEAR_PIN_CODE";
  case 0x08:
    return "CLEAR_ALL_PIN_CODES";
  case 0x09:
    return "SET_USER_STATUS";
  case 0x14:
    return "SET_USER_TYPE";
  case 0x16:
    return "SET_RFID_CODE";
  case 0x18:
    return "CLEAR_RFID_CODE";
  case 0x19:
    return "CLEAR_ALL_RFID_CODES";
  default:
    return "UNKNOWN";
  }
}

static const char *sdf_app_enrollment_state_name(sdf_enrollment_state_t state) {
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
sdf_app_enrollment_result_name(sdf_enrollment_result_t result) {
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

static uint8_t sdf_app_choose_fingerprint_permission(
    const sdf_protocol_zigbee_programming_event_t *pe) {
  if (pe == NULL) {
    return 1;
  }

  if (pe->has_user_type && pe->user_type >= 1u && pe->user_type <= 3u) {
    return pe->user_type;
  }

  if (pe->has_user_status && pe->user_status >= 1u && pe->user_status <= 3u) {
    return pe->user_status;
  }

  return 1;
}

static const char *
sdf_app_power_wake_reason_name(sdf_power_wake_reason_t reason) {
  switch (reason) {
  case SDF_POWER_WAKE_REASON_TIMER:
    return "timer";
  case SDF_POWER_WAKE_REASON_FINGERPRINT:
    return "fingerprint";
  case SDF_POWER_WAKE_REASON_OTHER:
    return "other";
  case SDF_POWER_WAKE_REASON_NONE:
  default:
    return "none";
  }
}

static void sdf_app_update_battery_percent(uint8_t battery_percent) {
  esp_err_t err = sdf_power_set_battery_percent(battery_percent);
  if (err != ESP_OK) {
    sdf_protocol_zigbee_update_battery_percent(battery_percent);
  }
}

static bool sdf_app_power_busy(void *ctx) {
  (void)ctx;
  if (s_pairing_active || s_latch_sequence_active) {
    return true;
  }
  if (s_lock_flow.state != SDF_LOCK_FLOW_IDLE) {
    return true;
  }
  return sdf_services_is_enrollment_active();
}

static void sdf_app_power_wakeup(void *ctx, sdf_power_wake_reason_t reason) {
  (void)ctx;
  sdf_power_mark_activity();
  ESP_LOGI(TAG, "Power wake event: %s", sdf_app_power_wake_reason_name(reason));

  if (reason == SDF_POWER_WAKE_REASON_TIMER && s_has_creds &&
      !s_pairing_active && sdf_nuki_ble_is_ready(&s_ble)) {
    int res = sdf_app_request_keyturner_state();
    if (res != SDF_NUKI_RESULT_OK) {
      ESP_LOGD(TAG, "Periodic keyturner refresh skipped: %d", res);
    }
  }
}

static int sdf_app_power_battery_percent(void *ctx) {
  (void)ctx;
  return sdf_drivers_battery_get_percent();
}

static void
sdf_app_on_security_event(void *ctx,
                          const sdf_services_security_event_t *event) {
  (void)ctx;
  if (event == NULL) {
    return;
  }

  switch (event->type) {
  case SDF_SERVICES_SECURITY_EVENT_MATCH_FAILED:
    sdf_app_emit_audit(SDF_AUDIT_BIOMETRIC_FAILED, 0,
                       (int32_t)event->failed_attempts,
                       (uint16_t)(event->lockout_remaining_ms > 0xFFFFu
                                      ? 0xFFFFu
                                      : event->lockout_remaining_ms));
    break;
  case SDF_SERVICES_SECURITY_EVENT_LOCKOUT_ENTERED:
    sdf_app_set_alarm_mask_bits(SDF_APP_ZB_ALARM_BIOMETRIC_LOCKOUT, 0);
    sdf_app_emit_audit(SDF_AUDIT_BIOMETRIC_LOCKOUT, 0,
                       (int32_t)event->failed_attempts,
                       (uint16_t)(event->lockout_remaining_ms > 0xFFFFu
                                      ? 0xFFFFu
                                      : event->lockout_remaining_ms));
    break;
  case SDF_SERVICES_SECURITY_EVENT_LOCKOUT_CLEARED:
    sdf_app_set_alarm_mask_bits(0, SDF_APP_ZB_ALARM_BIOMETRIC_LOCKOUT);
    sdf_app_emit_audit(SDF_AUDIT_BIOMETRIC_LOCKOUT_CLEARED, 0, 0, 0);
    break;
  case SDF_SERVICES_SECURITY_EVENT_MATCH_SUCCEEDED:
    sdf_app_emit_audit(SDF_AUDIT_BIOMETRIC_MATCH_SUCCESS, event->user_id, 0, 0);
    break;
  default:
    break;
  }
}

static void sdf_app_on_admin_action(void *ctx,
                                    sdf_services_admin_action_t action) {
  (void)ctx;
  switch (action) {
  case SDF_SERVICES_ADMIN_ACTION_NUKI_PAIR:
    ESP_LOGI(TAG, "Admin authorized Nuki Pairing");
    if (!s_has_creds && sdf_nuki_ble_is_ready(&s_ble) && !s_pairing_active) {
      int res = sdf_nuki_pairing_init(&s_pairing, &s_client, 0, SDF_APP_ID,
                                      SDF_APP_NAME);
      if (res == SDF_NUKI_RESULT_OK) {
        s_pairing_active = true;
        res = sdf_nuki_pairing_start(&s_pairing);
        if (res != SDF_NUKI_RESULT_OK) {
          ESP_LOGE(TAG, "Pairing start failed: %d", res);
          sdf_app_emit_audit(SDF_AUDIT_PAIRING_FAILED, 0, res, 1);
          s_pairing_active = false;
        }
      } else {
        ESP_LOGE(TAG, "Pairing init failed: %d", res);
        sdf_app_emit_audit(SDF_AUDIT_PAIRING_FAILED, 0, res, 0);
      }
    } else {
      ESP_LOGW(
          TAG,
          "Cannot start Nuki pairing: already have creds or BLE not ready");
    }
    break;

  case SDF_SERVICES_ADMIN_ACTION_ZB_JOIN:
    ESP_LOGI(TAG, "Admin authorized Zigbee Join");
    sdf_protocol_zigbee_permit_join();
    break;

  case SDF_SERVICES_ADMIN_ACTION_FACTORY_RESET:
    ESP_LOGI(TAG, "Admin authorized Factory Reset");
    // TODO: implement actual factory reset
    ESP_LOGI(TAG, "Factory reset not fully implemented yet in sdf_app");
    break;

  default:
    break;
  }
}

static int sdf_app_on_fingerprint_unlock(void *ctx, uint16_t user_id) {
  (void)ctx;
  sdf_power_mark_activity();

  if (!s_has_creds || s_pairing_active || !sdf_nuki_ble_is_ready(&s_ble)) {
    return SDF_NUKI_RESULT_ERR_NO_KEY;
  }

  int percent = sdf_drivers_battery_get_percent();
  if (percent <= 20) {
    sdf_services_trigger_low_battery_warning();
  }

  ESP_LOGI(TAG, "Fingerprint match for user_id=%u, requesting unlock",
           (unsigned)user_id);
  return sdf_app_lock_action(SDF_LOCK_ACTION_UNLOCK, 0);
}

static void sdf_app_on_enrollment_state(void *ctx,
                                        const sdf_enrollment_sm_t *state) {
  (void)ctx;
  sdf_power_mark_activity();

  if (state == NULL) {
    return;
  }

  ESP_LOGI(TAG,
           "Enrollment user_id=%u permission=%u state=%s completed_steps=%u "
           "result=%s",
           (unsigned)state->user_id, (unsigned)state->permission,
           sdf_app_enrollment_state_name(state->state),
           (unsigned)state->completed_steps,
           sdf_app_enrollment_result_name(state->result));

  if (state->state == SDF_ENROLLMENT_STATE_SUCCESS) {
    sdf_app_set_alarm_mask_bits(0, SDF_APP_ZB_ALARM_ACTION_FAILURE);
  } else if (state->state == SDF_ENROLLMENT_STATE_ERROR) {
    sdf_app_set_alarm_mask_bits(SDF_APP_ZB_ALARM_ACTION_FAILURE, 0);
  }
}

static int sdf_app_start_unlock_unlatch_sequence(void) {
  if (s_latch_sequence_active || s_lock_flow.state != SDF_LOCK_FLOW_IDLE) {
    return SDF_NUKI_RESULT_ERR_INCOMPLETE;
  }

  int res = sdf_app_lock_action(SDF_LOCK_ACTION_UNLOCK, 0);
  if (res == SDF_NUKI_RESULT_OK) {
    s_latch_sequence_active = true;
    ESP_LOGI(TAG, "Started latch sequence: unlock -> unlatch");
  }
  return res;
}

static void sdf_app_update_zigbee_user_list(void) {
  uint16_t user_ids[SDF_FINGERPRINT_USER_ID_MAX + 1];
  uint8_t perms[SDF_FINGERPRINT_USER_ID_MAX + 1];
  size_t count = 0;

  esp_err_t err = sdf_services_query_users(user_ids, perms, &count,
                                           SDF_FINGERPRINT_USER_ID_MAX + 1);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Failed to query active users for Zigbee sync: %s",
             esp_err_to_name(err));
    return;
  }

  /* Serialize to CSV-like bracketed list: e.g. [1:3, 5:1] meaning ID 1 (perm
   * 3), ID 5 (perm 1) */
  char buf[255];
  size_t offset = 0;
  buf[offset++] = '[';
  for (size_t i = 0; i < count; i++) {
    int written =
        snprintf(&buf[offset], sizeof(buf) - offset, "%s%u:%u",
                 i > 0 ? ", " : "", (unsigned)user_ids[i], (unsigned)perms[i]);
    if (written > 0 && (size_t)written < sizeof(buf) - offset) {
      offset += written;
    } else {
      break; // truncation
    }
  }
  if (offset < sizeof(buf)) {
    buf[offset++] = ']';
    buf[offset] = '\0';
  } else {
    buf[sizeof(buf) - 2] = ']';
    buf[sizeof(buf) - 1] = '\0';
  }

  sdf_protocol_zigbee_update_user_list(buf);
  ESP_LOGI(TAG, "Synced active users to Zigbee: %s", buf);
}

static esp_err_t
sdf_app_on_zigbee_command(void *ctx,
                          const sdf_protocol_zigbee_command_event_t *event) {
  (void)ctx;
  sdf_power_mark_activity();

  if (event == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  int res = SDF_NUKI_RESULT_ERR_ARG;
  switch (event->command) {
  case SDF_PROTOCOL_ZIGBEE_COMMAND_LOCK:
    ESP_LOGI(TAG, "Received Zigbee lock command");
    res = sdf_app_lock_action(SDF_LOCK_ACTION_LOCK, 0);
    break;
  case SDF_PROTOCOL_ZIGBEE_COMMAND_UNLOCK:
    ESP_LOGI(TAG, "Received Zigbee unlock command");
    res = sdf_app_lock_action(SDF_LOCK_ACTION_UNLOCK, 0);
    break;
  case SDF_PROTOCOL_ZIGBEE_COMMAND_LATCH:
    ESP_LOGI(TAG, "Received Zigbee latch command (unlock + unlatch)");
    res = sdf_app_start_unlock_unlatch_sequence();
    break;
  case SDF_PROTOCOL_ZIGBEE_COMMAND_PROGRAMMING_EVENT: {
    const sdf_protocol_zigbee_programming_event_t *pe =
        &event->programming_event;
    ESP_LOGI(TAG,
             "Programming command 0x%02X (%s), src=0x%04X/%u, user_id=%s%u, "
             "user_status=%s%u, user_type=%s%u, credential_len=%u",
             (unsigned)pe->zcl_command_id,
             sdf_app_zb_programming_cmd_name(pe->zcl_command_id),
             (unsigned)pe->src_short_addr, (unsigned)pe->src_endpoint,
             pe->has_user_id ? "" : "n/a:",
             (unsigned)(pe->has_user_id ? pe->user_id : 0),
             pe->has_user_status ? "" : "n/a:",
             (unsigned)(pe->has_user_status ? pe->user_status : 0),
             pe->has_user_type ? "" : "n/a:",
             (unsigned)(pe->has_user_type ? pe->user_type : 0),
             (unsigned)(pe->has_credential ? pe->credential_len : 0));

    if (pe->zcl_command_id == 0x05 || pe->zcl_command_id == 0x16) {
      if (!pe->has_user_id) {
        ESP_LOGW(TAG, "Enrollment command without user_id");
        return ESP_ERR_INVALID_ARG;
      }

      uint8_t permission = sdf_app_choose_fingerprint_permission(pe);
      esp_err_t enroll_err =
          sdf_services_request_enrollment(pe->user_id, permission);
      if (enroll_err != ESP_OK) {
        ESP_LOGW(TAG,
                 "Failed to queue enrollment for user_id=%u permission=%u: %s",
                 (unsigned)pe->user_id, (unsigned)permission,
                 esp_err_to_name(enroll_err));
        sdf_app_set_alarm_mask_bits(SDF_APP_ZB_ALARM_ACTION_FAILURE, 0);
        return enroll_err;
      }

      ESP_LOGI(TAG,
               "Enrollment requested from Zigbee for user_id=%u permission=%u",
               (unsigned)pe->user_id, (unsigned)permission);
    } else if (pe->zcl_command_id == 0x06 || pe->zcl_command_id == 0x17) {
      if (!pe->has_user_id) {
        ESP_LOGW(TAG, "Delete command without user_id");
        return ESP_ERR_INVALID_ARG;
      }

      esp_err_t del_err = sdf_services_delete_user(pe->user_id);
      if (del_err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to delete user_id=%u: %s", (unsigned)pe->user_id,
                 esp_err_to_name(del_err));
        sdf_app_set_alarm_mask_bits(SDF_APP_ZB_ALARM_ACTION_FAILURE, 0);
        return del_err;
      }
      ESP_LOGI(TAG, "Deleted user_id=%u successfully", (unsigned)pe->user_id);
      sdf_app_update_zigbee_user_list();
    } else if (pe->zcl_command_id == 0x07 || pe->zcl_command_id == 0x18) {
      esp_err_t clr_err = sdf_services_clear_all_users();
      if (clr_err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to clear all users: %s",
                 esp_err_to_name(clr_err));
        sdf_app_set_alarm_mask_bits(SDF_APP_ZB_ALARM_ACTION_FAILURE, 0);
        return clr_err;
      }
      ESP_LOGI(TAG, "Cleared all users successfully");
      sdf_app_update_zigbee_user_list();
    } else {
      ESP_LOGI(TAG, "Programming command 0x%02X currently mapped as no-op",
               (unsigned)pe->zcl_command_id);
    }
    return ESP_OK;
  }
  default:
    return ESP_ERR_INVALID_ARG;
  }

  if (res != SDF_NUKI_RESULT_OK) {
    ESP_LOGW(TAG, "Unable to execute Zigbee command, lock action result=%d",
             res);
    sdf_app_set_alarm_mask_bits(SDF_APP_ZB_ALARM_ACTION_FAILURE, 0);
    return ESP_FAIL;
  }

  return ESP_OK;
}

static const char *sdf_app_error_name(int8_t code) {
  switch ((uint8_t)code) {
  case 0x45:
    return "K_ERROR_BUSY";
  case 0x46:
    return "K_ERROR_CANCELED";
  case 0x47:
    return "K_ERROR_NOT_CALIBRATED";
  case 0x49:
    return "K_ERROR_MOTOR_LOW_VOLTAGE";
  case 0x4A:
    return "K_ERROR_MOTOR_POWER_FAILURE";
  case 0x4B:
    return "K_ERROR_CLUTCH_POWER_FAILURE";
  default:
    return "UNKNOWN_ERROR";
  }
}

static bool sdf_app_valid_lock_action(uint8_t lock_action) {
  switch (lock_action) {
  case SDF_LOCK_ACTION_UNLOCK:
  case SDF_LOCK_ACTION_LOCK:
  case SDF_LOCK_ACTION_UNLATCH:
  case SDF_LOCK_ACTION_LOCK_N_GO:
  case SDF_LOCK_ACTION_LOCK_N_GO_UNLATCH:
  case SDF_LOCK_ACTION_FULL_LOCK:
    return true;
  default:
    return false;
  }
}

static void sdf_app_emit_event(const sdf_event_t *event) {
  if (s_event_cb != NULL && event != NULL) {
    s_event_cb(s_event_ctx, event);
  }
}

static const char *sdf_app_audit_event_name(sdf_audit_event_type_t type) {
  switch (type) {
  case SDF_AUDIT_STORAGE_POLICY_OK:
    return "STORAGE_POLICY_OK";
  case SDF_AUDIT_STORAGE_POLICY_FAILED:
    return "STORAGE_POLICY_FAILED";
  case SDF_AUDIT_BIOMETRIC_FAILED:
    return "BIOMETRIC_FAILED";
  case SDF_AUDIT_BIOMETRIC_LOCKOUT:
    return "BIOMETRIC_LOCKOUT";
  case SDF_AUDIT_BIOMETRIC_LOCKOUT_CLEARED:
    return "BIOMETRIC_LOCKOUT_CLEARED";
  case SDF_AUDIT_BIOMETRIC_MATCH_SUCCESS:
    return "BIOMETRIC_MATCH_SUCCESS";
  case SDF_AUDIT_NONCE_REPLAY_BLOCKED:
    return "NONCE_REPLAY_BLOCKED";
  case SDF_AUDIT_PROTOCOL_ERROR:
    return "PROTOCOL_ERROR";
  case SDF_AUDIT_PAIRING_COMPLETE:
    return "PAIRING_COMPLETE";
  case SDF_AUDIT_PAIRING_FAILED:
    return "PAIRING_FAILED";
  default:
    return "UNKNOWN";
  }
}

static void sdf_app_emit_audit(sdf_audit_event_type_t type, uint16_t user_id,
                               int32_t status, uint16_t detail) {
  sdf_audit_event_t event = {
      .timestamp_ms = (uint64_t)(esp_timer_get_time() / 1000LL),
      .type = type,
      .user_id = user_id,
      .status = status,
      .detail = detail,
  };

  ESP_LOGI(TAG, "AUDIT %s user=%u status=%ld detail=%u",
           sdf_app_audit_event_name(type), (unsigned)event.user_id,
           event.status, (unsigned)event.detail);

  switch (type) {
  case SDF_AUDIT_BIOMETRIC_FAILED:
    s_app_audit_err_biometric_failed++;
    break;
  case SDF_AUDIT_BIOMETRIC_LOCKOUT:
    s_app_audit_err_auth_lockout++;
    break;
  case SDF_AUDIT_NONCE_REPLAY_BLOCKED:
    s_app_audit_err_nonce_replay++;
    break;
  case SDF_AUDIT_PROTOCOL_ERROR:
    s_app_audit_err_protocol++;
    break;
  default:
    break;
  }

  if (s_audit_cb != NULL) {
    s_audit_cb(s_audit_ctx, &event);
  }
}

/* ---- Lock flow callbacks (bridge to app-level state) ---- */

static int sdf_app_lf_send_challenge(void *ctx) {
  (void)ctx;
  return sdf_nuki_client_send_request_data(&s_client, SDF_NUKI_CMD_CHALLENGE,
                                           NULL, 0);
}

static int sdf_app_lf_send_action(void *ctx, uint8_t action, uint8_t flags,
                                  const uint8_t *nonce_nk) {
  (void)ctx;
  return sdf_nuki_client_send_lock_action(
      &s_client, action, flags, (const uint8_t *)SDF_APP_LOCK_SUFFIX,
      strlen(SDF_APP_LOCK_SUFFIX), nonce_nk);
}

static void sdf_app_lf_on_fail(void *ctx, const char *reason) {
  (void)ctx;
  sdf_app_abort_latch_sequence(reason);
  sdf_app_set_alarm_mask_bits(SDF_APP_ZB_ALARM_ACTION_FAILURE, 0);
}

static void sdf_app_lf_on_progress(void *ctx, bool in_progress, uint8_t action,
                                   uint8_t retries) {
  (void)ctx;
  sdf_event_t event;
  memset(&event, 0, sizeof(event));
  event.type = SDF_EVENT_LOCK_ACTION_PROGRESS;
  event.lock_action_in_progress = in_progress;
  event.lock_action = action;
  event.retry_count = retries;
  sdf_app_emit_event(&event);
}

static void sdf_app_lf_on_complete(void *ctx, uint8_t action) {
  (void)ctx;
  sdf_app_set_alarm_mask_bits(0, SDF_APP_ZB_ALARM_ACTION_FAILURE);
  sdf_app_update_zigbee_from_action(action);

  if (s_latch_sequence_active && action == SDF_LOCK_ACTION_UNLOCK) {
    int res = sdf_app_lock_action(SDF_LOCK_ACTION_UNLATCH, 0);
    if (res == SDF_NUKI_RESULT_OK) {
      ESP_LOGI(TAG, "Latch sequence continuing with unlatch");
      return;
    }
    ESP_LOGW(TAG, "Failed to start unlatch step in latch sequence: %d", res);
    sdf_app_abort_latch_sequence("failed to start unlatch");
    sdf_app_set_alarm_mask_bits(SDF_APP_ZB_ALARM_ACTION_FAILURE, 0);
  } else if (s_latch_sequence_active) {
    if (action == SDF_LOCK_ACTION_UNLATCH) {
      ESP_LOGI(TAG, "Latch sequence finished");
    }
    s_latch_sequence_active = false;
  }

  sdf_app_request_keyturner_state();
}

static int sdf_app_send_encrypted(void *ctx, const uint8_t *data, size_t len) {
  (void)ctx;
  return sdf_nuki_ble_send(&s_ble, SDF_NUKI_BLE_CHANNEL_USDIO, data, len);
}

static int sdf_app_send_unencrypted(void *ctx, const uint8_t *data,
                                    size_t len) {
  (void)ctx;
  return sdf_nuki_ble_send(&s_ble, SDF_NUKI_BLE_CHANNEL_GDIO, data, len);
}

int sdf_app_request_keyturner_state(void) {
  sdf_power_mark_activity();
  if (!s_has_creds || s_pairing_active || !sdf_nuki_ble_is_ready(&s_ble)) {
    return SDF_NUKI_RESULT_ERR_ARG;
  }

  return sdf_nuki_client_send_request_data(
      &s_client, SDF_NUKI_CMD_KEYTURNER_STATES, NULL, 0);
}

int sdf_app_lock_action(uint8_t lock_action, uint8_t flags) {
  sdf_power_mark_activity();
  if (!s_has_creds || s_pairing_active || !sdf_nuki_ble_is_ready(&s_ble)) {
    return SDF_NUKI_RESULT_ERR_ARG;
  }

  if (!sdf_app_valid_lock_action(lock_action)) {
    return SDF_NUKI_RESULT_ERR_ARG;
  }

  return sdf_lock_flow_begin(&s_lock_flow, lock_action, flags);
}

void sdf_app_set_event_callback(sdf_event_cb cb, void *ctx) {
  s_event_cb = cb;
  s_event_ctx = ctx;
}

void sdf_app_set_audit_callback(sdf_audit_cb cb, void *ctx) {
  s_audit_cb = cb;
  s_audit_ctx = ctx;
}

static void sdf_app_on_message(void *ctx, const sdf_nuki_message_t *msg) {
  (void)ctx;
  sdf_power_mark_activity();

  if (msg == NULL) {
    return;
  }

  sdf_app_set_alarm_mask_bits(0, SDF_APP_ZB_ALARM_SECURITY_PROTOCOL);

  if (msg->command_id == SDF_NUKI_CMD_CHALLENGE) {
    sdf_lock_flow_on_challenge(&s_lock_flow, msg);
    return;
  }

  if (msg->command_id == SDF_NUKI_CMD_STATUS) {
    uint8_t status = 0;
    if (sdf_nuki_parse_status(msg, &status) != SDF_NUKI_RESULT_OK) {
      return;
    }

    ESP_LOGI(TAG, "Status 0x%02X (%s)", status, sdf_app_status_name(status));

    sdf_event_t event;
    memset(&event, 0, sizeof(event));
    event.type = SDF_EVENT_STATUS;
    event.data.status = status;
    event.lock_action_in_progress = s_lock_flow.state != SDF_LOCK_FLOW_IDLE;
    event.lock_action = s_lock_flow.requested_action;
    event.retry_count = s_lock_flow.retry_count;
    sdf_app_emit_event(&event);

    sdf_lock_flow_on_status(&s_lock_flow, status);
    return;
  }

  if (msg->command_id == SDF_NUKI_CMD_KEYTURNER_STATES) {
    sdf_keyturner_state_t state;
    if (sdf_nuki_parse_keyturner_states(msg, &state) != SDF_NUKI_RESULT_OK) {
      return;
    }

    ESP_LOGI(TAG,
             "Keyturner state: nuki=%u lock=%u trigger=%u battery_critical=%u",
             (unsigned)state.nuki_state, (unsigned)state.lock_state,
             (unsigned)state.trigger, (unsigned)state.critical_battery_state);

    sdf_event_t event;
    memset(&event, 0, sizeof(event));
    event.type = SDF_EVENT_KEYTURNER_STATE;
    event.data.keyturner_state = state;
    event.lock_action_in_progress = s_lock_flow.state != SDF_LOCK_FLOW_IDLE;
    event.lock_action = s_lock_flow.requested_action;
    event.retry_count = s_lock_flow.retry_count;
    sdf_app_emit_event(&event);

    sdf_protocol_zigbee_update_lock_state(
        sdf_app_map_lock_state_to_zigbee(state.lock_state));
    if (state.critical_battery_state) {
      sdf_app_set_alarm_mask_bits(SDF_APP_ZB_ALARM_LOW_BATTERY, 0);
    } else {
      sdf_app_set_alarm_mask_bits(0, SDF_APP_ZB_ALARM_LOW_BATTERY);
    }
    return;
  }

  if (msg->command_id == SDF_NUKI_CMD_ERROR_REPORT) {
    sdf_error_report_t report;
    if (sdf_nuki_parse_error_report(msg, &report) != SDF_NUKI_RESULT_OK) {
      return;
    }

    ESP_LOGW(TAG, "Error report code=0x%02X (%s) cmd=0x%04X",
             (unsigned)((uint8_t)report.error_code),
             sdf_app_error_name(report.error_code), report.command_identifier);

    sdf_event_t event;
    memset(&event, 0, sizeof(event));
    event.type = SDF_EVENT_ERROR;
    event.data.error_report = report;
    event.lock_action_in_progress = s_lock_flow.state != SDF_LOCK_FLOW_IDLE;
    event.lock_action = s_lock_flow.requested_action;
    event.retry_count = s_lock_flow.retry_count;
    sdf_app_emit_event(&event);
    sdf_app_set_alarm_mask_bits(SDF_APP_ZB_ALARM_ACTION_FAILURE, 0);

    sdf_lock_flow_on_error(&s_lock_flow);
    return;
  }

  ESP_LOGI(TAG, "Nuki message cmd=0x%04X len=%u", msg->command_id,
           (unsigned)msg->payload_len);
}

static void sdf_app_on_ble_ready(void *ctx) {
  (void)ctx;
  sdf_power_mark_activity();
  ESP_LOGI(TAG, "BLE transport ready");

  if (!sdf_lock_flow_is_idle(&s_lock_flow)) {
    ESP_LOGW(TAG, "Resetting stale lock action flow after reconnect");
    sdf_app_abort_latch_sequence("BLE reconnect during action");
    sdf_lock_flow_reset(&s_lock_flow);
    sdf_app_lf_on_progress(NULL, false, 0, 0);
  }

  if (!s_has_creds) {
    ESP_LOGI(TAG,
             "No Nuki credentials found. Waiting for Admin Action to pair.");
    return;
  }

  int res = sdf_app_request_keyturner_state();
  if (res != SDF_NUKI_RESULT_OK) {
    ESP_LOGW(TAG, "Initial keyturner state request failed: %d", res);
  }
}

static void sdf_app_on_ble_rx(void *ctx, sdf_nuki_ble_channel_t channel,
                              const uint8_t *data, size_t len) {
  (void)ctx;
  sdf_power_mark_activity();

  if (channel == SDF_NUKI_BLE_CHANNEL_GDIO) {
    if (s_pairing_active) {
      int pairing_res =
          sdf_nuki_pairing_handle_unencrypted(&s_pairing, data, len);
      if (pairing_res != SDF_NUKI_RESULT_OK &&
          pairing_res != SDF_NUKI_RESULT_ERR_INCOMPLETE) {
        ESP_LOGW(TAG, "Pairing GDIO handling failed: %d", pairing_res);
        sdf_app_emit_audit(SDF_AUDIT_PAIRING_FAILED, 0, pairing_res, 2);
      }
    }
    return;
  }

  if (s_pairing_active) {
    int pairing_res = sdf_nuki_pairing_handle_encrypted(&s_pairing, data, len);
    if (pairing_res != SDF_NUKI_RESULT_OK &&
        pairing_res != SDF_NUKI_RESULT_ERR_INCOMPLETE) {
      ESP_LOGW(TAG, "Pairing USDIO handling failed: %d", pairing_res);
      sdf_app_emit_audit(SDF_AUDIT_PAIRING_FAILED, 0, pairing_res, 3);
    }

    if (s_pairing.state == SDF_NUKI_PAIRING_COMPLETE) {
      sdf_nuki_credentials_t creds;
      if (sdf_nuki_pairing_get_credentials(&s_pairing, &creds) ==
          SDF_NUKI_RESULT_OK) {
        s_client.creds = creds;
        s_has_creds = true;
        s_pairing_active = false;
        sdf_app_emit_audit(SDF_AUDIT_PAIRING_COMPLETE, 0,
                           (int32_t)creds.authorization_id, 0);
        esp_err_t err =
            sdf_storage_nuki_save(creds.authorization_id, creds.shared_key);
        if (err != ESP_OK) {
          ESP_LOGE(TAG, "Failed to save credentials: %s", esp_err_to_name(err));
          sdf_app_emit_audit(SDF_AUDIT_STORAGE_POLICY_FAILED, 0, err, 1);
        }
        ESP_LOGI(TAG, "Pairing complete; credentials stored");
        int res = sdf_app_request_keyturner_state();
        if (res != SDF_NUKI_RESULT_OK) {
          ESP_LOGW(TAG, "Post-pair keyturner state request failed: %d", res);
        }
      }
    }
    return;
  }

  int feed_res = sdf_nuki_client_feed_encrypted(&s_client, data, len);
  if (feed_res == SDF_NUKI_RESULT_OK ||
      feed_res == SDF_NUKI_RESULT_ERR_INCOMPLETE) {
    return;
  }

  if (feed_res == SDF_NUKI_RESULT_ERR_NONCE_REUSE) {
    ESP_LOGW(TAG, "Rejected replayed encrypted nonce");
    sdf_app_set_alarm_mask_bits(SDF_APP_ZB_ALARM_SECURITY_PROTOCOL, 0);
    sdf_app_emit_audit(SDF_AUDIT_NONCE_REPLAY_BLOCKED, 0, feed_res, 0);
    return;
  }

  ESP_LOGW(TAG, "Encrypted message handling failed: %d", feed_res);
  sdf_app_set_alarm_mask_bits(SDF_APP_ZB_ALARM_SECURITY_PROTOCOL, 0);
  sdf_app_emit_audit(SDF_AUDIT_PROTOCOL_ERROR, 0, feed_res, 0);

  if (!sdf_lock_flow_is_idle(&s_lock_flow) &&
      (feed_res == SDF_NUKI_RESULT_ERR_CRC ||
       feed_res == SDF_NUKI_RESULT_ERR_AUTH)) {
    sdf_lock_flow_retry(&s_lock_flow, "encrypted frame validation failed");
  }
}

void sdf_app_init(void) {
  static const sdf_lock_flow_ops_t lf_ops = {
      .send_challenge = sdf_app_lf_send_challenge,
      .send_action = sdf_app_lf_send_action,
      .on_fail = sdf_app_lf_on_fail,
      .on_progress = sdf_app_lf_on_progress,
      .on_complete = sdf_app_lf_on_complete,
      .ctx = NULL,
  };
  sdf_lock_flow_init(&s_lock_flow, SDF_APP_LOCK_ACTION_MAX_RETRIES, &lf_ops);
  s_zigbee_alarm_mask = 0;
  s_latch_sequence_active = false;

  esp_err_t err = sdf_storage_init();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Storage init failed: %s", esp_err_to_name(err));
    sdf_app_emit_audit(SDF_AUDIT_STORAGE_POLICY_FAILED, 0, err, 0);
  }

  err = sdf_drivers_battery_adc_init(SDF_APP_BATTERY_ADC_GPIO);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Battery ADC init failed: %s", esp_err_to_name(err));
  }

#ifndef CONFIG_IDF_TARGET_LINUX
  esp_task_wdt_config_t twdt_config = {
      .timeout_ms = SDF_APP_TWDT_TIMEOUT_MS,
      .idle_core_mask = (1 << portNUM_PROCESSORS) - 1,
      .trigger_panic = true,
  };
  esp_err_t twdt_err = esp_task_wdt_reconfigure(&twdt_config);
  if (twdt_err != ESP_OK) {
    ESP_LOGW(TAG, "Failed to reconfigure TWDT: %s", esp_err_to_name(twdt_err));
  }
#endif

  sdf_storage_security_status_t storage_security = {0};
  if (sdf_storage_get_security_status(&storage_security) == ESP_OK) {
    uint16_t security_bits = 0;
    if (storage_security.require_encrypted_nvs) {
      security_bits |= 0x01u;
    }
    if (storage_security.nvs_encryption_enabled) {
      security_bits |= 0x02u;
    }
    if (storage_security.nvs_keys_partition_present) {
      security_bits |= 0x04u;
    }
    if (storage_security.nvs_keys_accessible) {
      security_bits |= 0x08u;
    }

    if (sdf_storage_nvs_security_ok()) {
      sdf_app_emit_audit(SDF_AUDIT_STORAGE_POLICY_OK, 0, 0, security_bits);
    } else {
      sdf_app_emit_audit(SDF_AUDIT_STORAGE_POLICY_FAILED, 0, -1, security_bits);
    }
  }

  sdf_nuki_credentials_t creds = {0};
  uint32_t auth_id = 0;
  uint8_t shared_key[32] = {0};

  if (sdf_storage_nuki_load(&auth_id, shared_key) == ESP_OK) {
    creds.authorization_id = auth_id;
    memcpy(creds.shared_key, shared_key, sizeof(shared_key));
    creds.app_id = SDF_APP_ID;
    s_has_creds = true;
    ESP_LOGI(TAG, "Loaded stored Nuki credentials");
  }
  mbedtls_platform_zeroize(shared_key, sizeof(shared_key));

  sdf_nuki_client_init(&s_client, &creds, sdf_app_send_encrypted, NULL,
                       sdf_app_send_unencrypted, NULL, sdf_app_on_message,
                       NULL);

  sdf_services_config_t services_cfg;
  sdf_services_get_default_config(&services_cfg);
  services_cfg.fingerprint.uart_port = SDF_APP_FP_UART_PORT;
  services_cfg.fingerprint.tx_pin = SDF_APP_FP_UART_TX_PIN;
  services_cfg.fingerprint.rx_pin = SDF_APP_FP_UART_RX_PIN;
  services_cfg.match_poll_interval_ms = SDF_APP_FP_MATCH_POLL_MS;
  services_cfg.match_cooldown_ms = SDF_APP_FP_MATCH_COOLDOWN_MS;
  services_cfg.unlock_cb = sdf_app_on_fingerprint_unlock;
  services_cfg.unlock_ctx = NULL;
  services_cfg.enrollment_cb = sdf_app_on_enrollment_state;
  services_cfg.enrollment_ctx = NULL;
  services_cfg.admin_action_cb = sdf_app_on_admin_action;
  services_cfg.admin_action_ctx = NULL;
  services_cfg.security_event_cb = sdf_app_on_security_event;
  services_cfg.security_event_ctx = NULL;
  services_cfg.wake_gpio = SDF_APP_FP_WAKE_GPIO;
  services_cfg.power_en_gpio = SDF_APP_FP_POWER_EN_GPIO;
  services_cfg.enrollment_btn_gpio = SDF_APP_ENROLLMENT_BTN_GPIO;
  services_cfg.ws2812_led_gpio = SDF_APP_WS2812_LED_GPIO;

  err = sdf_services_init(&services_cfg);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Failed to initialize fingerprint services: %s",
             esp_err_to_name(err));
  }

  err = sdf_protocol_zigbee_set_checkin_interval_ms(
      SDF_APP_POWER_CHECKIN_INTERVAL_MS);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Failed to set Zigbee check-in interval: %s",
             esp_err_to_name(err));
  }

  err =
      sdf_protocol_zigbee_set_command_handler(sdf_app_on_zigbee_command, NULL);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Failed to set Zigbee command handler: %s",
             esp_err_to_name(err));
  }

  err = sdf_protocol_zigbee_init();
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Failed to start Zigbee protocol: %s", esp_err_to_name(err));
  } else {
    sdf_protocol_zigbee_update_lock_state(
        SDF_PROTOCOL_ZIGBEE_LOCK_STATE_UNDEFINED);
    sdf_app_update_battery_percent(SDF_APP_POWER_BATTERY_DEFAULT_PERCENT);
    sdf_protocol_zigbee_update_alarm_mask(0);
  }

  sdf_nuki_ble_init(&s_ble, sdf_app_on_ble_rx, NULL, sdf_app_on_ble_ready,
                    NULL);

  /* Load BLE target address from NVS, fall back to compile-time default */
  ble_addr_t ble_target = SDF_NUKI_TARGET_ADDR;
  {
    uint8_t stored_type = 0;
    uint8_t stored_addr[6] = {0};
    if (sdf_storage_ble_target_load(&stored_type, stored_addr) == ESP_OK) {
      ble_target.type = stored_type;
      memcpy(ble_target.val, stored_addr, sizeof(ble_target.val));
      ESP_LOGI(TAG, "Loaded BLE target address from NVS");
    } else {
      ESP_LOGI(TAG, "Using compile-time BLE target address");
    }
  }
  if (!sdf_nuki_ble_addr_is_empty(&ble_target)) {
    sdf_nuki_ble_set_target_addr(&s_ble, &ble_target);
    ESP_LOGI(TAG, "Starting BLE scan (target address set)");
  } else {
    ESP_LOGI(TAG,
             "Starting BLE scan (advertisement discovery, no target address)");
  }
  sdf_nuki_ble_start(&s_ble);

  sdf_power_manager_config_t power_cfg;
  sdf_power_get_default_power_config(&power_cfg);
  power_cfg.ble_transport = &s_ble;
  power_cfg.fingerprint_wake_gpio = SDF_APP_FP_WAKE_GPIO;
  power_cfg.checkin_interval_ms = SDF_APP_POWER_CHECKIN_INTERVAL_MS;
  power_cfg.idle_before_sleep_ms = SDF_APP_POWER_IDLE_BEFORE_SLEEP_MS;
  power_cfg.post_wake_guard_ms = SDF_APP_POWER_POST_WAKE_GUARD_MS;
  power_cfg.loop_interval_ms = SDF_APP_POWER_LOOP_INTERVAL_MS;
  power_cfg.battery_report_interval_ms = SDF_APP_POWER_BATTERY_REPORT_MS;
  power_cfg.enable_light_sleep = SDF_APP_POWER_ENABLE_LIGHT_SLEEP;
  power_cfg.enable_ble_radio_gating = SDF_APP_POWER_ENABLE_BLE_RADIO_GATING;
  power_cfg.enable_deep_sleep_fallback = true;
  power_cfg.battery_percent_default = SDF_APP_POWER_BATTERY_DEFAULT_PERCENT;
  power_cfg.busy_cb = sdf_app_power_busy;
  power_cfg.busy_ctx = NULL;
  power_cfg.wake_cb = sdf_app_power_wakeup;
  power_cfg.wake_ctx = NULL;
  power_cfg.battery_cb = sdf_app_power_battery_percent;
  power_cfg.battery_ctx = NULL;

  err = sdf_power_init_power_manager(&power_cfg);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Failed to start power manager: %s", esp_err_to_name(err));
  }

  sdf_power_mark_activity();
}

#ifdef SDF_APP_TESTING
#include "sdf_app_test_exports.inc"
#endif
