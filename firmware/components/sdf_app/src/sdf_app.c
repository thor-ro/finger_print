#include "sdf_app.h"
#include "sdf_platform.h"
#include "sdf_config.h"

#include <stdlib.h>
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
#include "battery.h"
#include "led.h"
#include "sdf_cli.h"
#include "sdf_power.h"
#include "sdf_protocol_ble.h"
#include "sdf_protocol_zigbee.h"
#include "sdf_services.h"
#include "sdf_event_router.h"
#include "sdf_storage.h"
#include "sdf_ble_companion.h"
#include "cJSON.h"

#define SDF_APP_ID 1u
#define SDF_APP_NAME "SDF"
#define SDF_APP_LOCK_SUFFIX "SDF"
#define SDF_APP_LOCK_ACTION_MAX_RETRIES 2u
#define SDF_APP_ZB_ALARM_ACTION_FAILURE 0x0001u
#define SDF_APP_ZB_ALARM_LOW_BATTERY 0x0002u
#define SDF_APP_ZB_ALARM_BIOMETRIC_LOCKOUT 0x0004u
#define SDF_APP_ZB_ALARM_SECURITY_PROTOCOL 0x0008u

#define SDF_APP_TWDT_TIMEOUT_MS 15000u

static const char *TAG = "sdf_app";

static sdf_nuki_ble_transport_t s_ble;
static sdf_nuki_client_t s_client;
static sdf_nuki_pairing_t s_pairing;
static bool s_has_creds;
static bool s_pairing_active;
static bool s_pairing_requested;
static uint16_t s_zigbee_alarm_mask;
// Diagnostic counters
static uint32_t s_app_audit_err_biometric_failed = 0;
static uint32_t s_app_audit_err_auth_lockout = 0;
static uint32_t s_app_audit_err_nonce_replay = 0;
static uint32_t s_app_audit_err_protocol = 0;
static bool s_latch_sequence_active;
static bool s_lock_action_pending;
static uint8_t s_pending_lock_action;
static uint8_t s_pending_lock_flags;

static sdf_lock_flow_t s_lock_flow;

void sdf_app_emit_audit(sdf_audit_event_type_t type, uint16_t user_id,
                               int32_t status, uint16_t detail);
static const char *sdf_app_audit_event_name(sdf_audit_event_type_t type);
static void sdf_app_start_requested_nuki_pairing(void);
static void sdf_app_resume_ble_transport(const char *reason);
static void sdf_app_release_ble_transport(const char *reason);
static int sdf_app_queue_lock_action(uint8_t lock_action, uint8_t flags);
static int sdf_app_dispatch_pending_lock_action(void);
static int sdf_app_start_unlock_unlatch_sequence(void);

static void sdf_app_on_web_reg_auth_request(void *ctx,
                                             const sdf_event_router_event_t *event);
static void sdf_app_on_web_reg_auth_result(void *ctx,
                                            const sdf_event_router_event_t *event);
static void sdf_app_on_web_reg_auth_request(void *ctx,
                                             const sdf_event_router_event_t *event);

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
  uint16_t new_mask = (s_zigbee_alarm_mask | set_bits) & (uint16_t)(~clear_bits);
  if (new_mask != s_zigbee_alarm_mask) {
    s_zigbee_alarm_mask = new_mask;
    if (sdf_protocol_zigbee_is_enabled()) {
      sdf_protocol_zigbee_update_alarm_mask(s_zigbee_alarm_mask);
    }
  }
}

static void sdf_app_abort_latch_sequence(const char *reason) {
  if (!s_latch_sequence_active) {
    return;
  }

  s_latch_sequence_active = false;
  ESP_LOGW(TAG, "Aborted latch sequence: %s", reason);
}

static void sdf_app_resume_ble_transport(const char *reason) {
  int res = sdf_nuki_ble_set_enabled(&s_ble, true);
  if (res != 0) {
    ESP_LOGW(TAG, "Failed to enable BLE transport for resume: %d", res);
  }

  sdf_nuki_ble_reset_backoff(&s_ble);

  res = sdf_nuki_ble_start(&s_ble);
  if (res != 0) {
    ESP_LOGW(TAG, "Failed to start BLE transport for resume: %d", res);
  }

  if (reason != NULL) {
    ESP_LOGI(TAG, "Requested BLE transport resume: %s", reason);
  }
}

static void sdf_app_release_ble_transport(const char *reason) {
  const sdf_config_t *cfg = sdf_config_get();
  if (!cfg->ble_connect_on_demand) {
    return;
  }
  if (s_pairing_active || s_pairing_requested || s_lock_action_pending ||
      s_latch_sequence_active || !sdf_lock_flow_is_idle(&s_lock_flow)) {
    return;
  }

  int res = sdf_nuki_ble_stop(&s_ble);
  if (res != 0) {
    ESP_LOGW(TAG, "Failed to release BLE transport: %d", res);
    return;
  }

  if (reason != NULL) {
    ESP_LOGI(TAG, "Released BLE transport: %s", reason);
  }
}

static int sdf_app_queue_lock_action(uint8_t lock_action, uint8_t flags) {
  if (s_lock_flow.state != SDF_LOCK_FLOW_IDLE) {
    return SDF_NUKI_RESULT_ERR_INCOMPLETE;
  }

  if (s_lock_action_pending) {
    if (s_pending_lock_action == lock_action && s_pending_lock_flags == flags) {
      sdf_app_resume_ble_transport("queued lock action retry");
      return SDF_NUKI_RESULT_OK;
    }
    return SDF_NUKI_RESULT_ERR_INCOMPLETE;
  }

  s_lock_action_pending = true;
  s_pending_lock_action = lock_action;
  s_pending_lock_flags = flags;

  ESP_LOGI(TAG,
           "BLE transport not ready; queued lock action 0x%02X until reconnect",
           (unsigned)lock_action);
  sdf_app_resume_ble_transport("queued lock action");
  return SDF_NUKI_RESULT_OK;
}

static int sdf_app_dispatch_pending_lock_action(void) {
  if (!s_lock_action_pending) {
    return SDF_NUKI_RESULT_OK;
  }

  if (!s_has_creds || s_pairing_active || !sdf_nuki_ble_is_ready(&s_ble)) {
    return SDF_NUKI_RESULT_ERR_INCOMPLETE;
  }

  int res =
      sdf_lock_flow_begin(&s_lock_flow, s_pending_lock_action, s_pending_lock_flags);
  if (res != SDF_NUKI_RESULT_OK) {
    ESP_LOGW(TAG,
             "Queued lock action 0x%02X could not start after BLE ready: %d",
             (unsigned)s_pending_lock_action, res);
    return res;
  }

  ESP_LOGI(TAG, "Started queued lock action 0x%02X after BLE ready",
           (unsigned)s_pending_lock_action);
  s_lock_action_pending = false;
  s_pending_lock_action = 0;
  s_pending_lock_flags = 0;
  return SDF_NUKI_RESULT_OK;
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
  if (!sdf_protocol_zigbee_is_enabled()) {
    return;
  }

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
  if (err == ESP_OK && sdf_protocol_zigbee_is_enabled()) {
    sdf_protocol_zigbee_update_battery_percent(battery_percent);
  }
}

static bool sdf_app_power_busy(void *ctx) {
  (void)ctx;
  if (s_pairing_active || s_pairing_requested || s_latch_sequence_active) {
    return true;
  }
  if (s_lock_action_pending) {
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

static void sdf_app_on_admin_action(void *ctx,
                                    sdf_services_admin_action_t action) {
  (void)ctx;
  switch (action) {
  case SDF_SERVICES_ADMIN_ACTION_NUKI_PAIR:
    ESP_LOGI(TAG, "Admin authorized Nuki Pairing");
    sdf_power_mark_activity();
    if (s_pairing_active || s_pairing_requested) {
      ESP_LOGW(TAG, "Cannot start Nuki pairing: pairing already active");
      led_flash_red();
      break;
    }

    s_pairing_requested = true;
    led_rapid_yellow();
    sdf_nuki_ble_set_enabled(&s_ble, true);
    sdf_nuki_ble_start(&s_ble);
    if (!sdf_nuki_ble_is_ready(&s_ble)) {
      ESP_LOGI(TAG,
               "Nuki pairing requested; waiting for BLE transport ready");
    }
    sdf_app_start_requested_nuki_pairing();
    break;

  case SDF_SERVICES_ADMIN_ACTION_ZB_JOIN:
    ESP_LOGI(TAG, "Admin authorized Zigbee Join");
    led_rapid_purple();
    esp_err_t err = sdf_protocol_zigbee_permit_join();
    if (err == ESP_ERR_NOT_SUPPORTED) {
      ESP_LOGI(TAG, "Ignoring Zigbee Join request because Zigbee is disabled");
    }
    break;

  case SDF_SERVICES_ADMIN_ACTION_FACTORY_RESET: {
    ESP_LOGI(TAG, "Admin authorized Factory Reset");
    // Execute complete factory reset sequence
    ESP_LOGI(TAG, "Step 1/5: Erasing all NVS data");
    esp_err_t err = sdf_storage_erase_all();
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "Failed to erase NVS: %s", esp_err_to_name(err));
    }
    
    ESP_LOGI(TAG, "Step 2/5: Deleting all fingerprint templates");
    sdf_fingerprint_op_result_t fp_result = fp_delete_all_users();
    if (fp_result != SDF_FINGERPRINT_OP_OK) {
      ESP_LOGE(TAG, "Failed to delete all fingerprint users: %d", (int)fp_result);
    }
    
    ESP_LOGI(TAG, "Step 3/5: Resetting Zigbee stack");
    err = sdf_protocol_zigbee_factory_reset();
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "Failed to reset Zigbee: %s", esp_err_to_name(err));
    }
    
    ESP_LOGI(TAG, "Step 4/5: Resetting services state");
    err = sdf_services_reset_state();
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "Failed to reset services state: %s", esp_err_to_name(err));
    }
    
    ESP_LOGI(TAG, "Step 5/5: Rebooting device");
    esp_restart();
    break;
  }

  default:
    break;
  }
}

static void sdf_ble_companion_on_auth_request(void *ctx,
                                               const char *username,
                                               const uint8_t *password_hash,
                                               size_t hash_len) {
    (void)ctx;
    ESP_LOGI(TAG, "BLE Companion: Auth request for user '%s'", username);

    sdf_event_router_event_t evt = {
        .type = SDF_EVENT_ROUTER_WEB_REG_AUTH_REQUEST,
        .priority = SDF_EVENT_ROUTER_PRIO_HIGH,
        .timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL),
    };
    strncpy(evt.payload.web_reg_auth_request.username, username,
            SDF_STORAGE_WEB_USER_NAME_MAX - 1);
    memcpy(evt.payload.web_reg_auth_request.password_hash, password_hash,
           SDF_STORAGE_WEB_USER_HASH_LEN);
    sdf_event_router_emit(&evt);
}

static void sdf_ble_companion_on_config_write(void *ctx,
                                               const uint8_t *data,
                                               size_t len) {
    (void)ctx;
    ESP_LOGI(TAG, "BLE Companion: Config write, len=%u", (unsigned)len);

    cJSON *root = cJSON_ParseWithLength((const char *)data, len);
    if (!root) {
        ESP_LOGW(TAG, "Config write: invalid JSON");
        return;
    }

    sdf_config_t *cfg = sdf_config_get_mutable();
    if (!cfg) {
        ESP_LOGW(TAG, "Config write: runtime overrides disabled");
        cJSON_Delete(root);
        return;
    }

    cJSON *item;

    item = cJSON_GetObjectItemCaseSensitive(root, "checkin_interval_ms");
    if (cJSON_IsNumber(item)) {
        cfg->checkin_interval_ms = (uint32_t)item->valuedouble;
        ESP_LOGI(TAG, "Config: checkin_interval_ms=%lu", (unsigned long)cfg->checkin_interval_ms);
    }

    item = cJSON_GetObjectItemCaseSensitive(root, "idle_before_sleep_ms");
    if (cJSON_IsNumber(item)) {
        cfg->idle_before_sleep_ms = (uint32_t)item->valuedouble;
        ESP_LOGI(TAG, "Config: idle_before_sleep_ms=%lu", (unsigned long)cfg->idle_before_sleep_ms);
    }

    item = cJSON_GetObjectItemCaseSensitive(root, "post_wake_guard_ms");
    if (cJSON_IsNumber(item)) {
        cfg->post_wake_guard_ms = (uint32_t)item->valuedouble;
        ESP_LOGI(TAG, "Config: post_wake_guard_ms=%lu", (unsigned long)cfg->post_wake_guard_ms);
    }

    item = cJSON_GetObjectItemCaseSensitive(root, "battery_default_percent");
    if (cJSON_IsNumber(item)) {
        cfg->battery_default_percent = (uint8_t)item->valuedouble;
        ESP_LOGI(TAG, "Config: battery_default_percent=%u", (unsigned)cfg->battery_default_percent);
    }

    item = cJSON_GetObjectItemCaseSensitive(root, "ble_connect_on_demand");
    if (cJSON_IsBool(item)) {
        cfg->ble_connect_on_demand = cJSON_IsTrue(item);
        ESP_LOGI(TAG, "Config: ble_connect_on_demand=%d", cfg->ble_connect_on_demand);
    }

    item = cJSON_GetObjectItemCaseSensitive(root, "match_poll_interval_ms");
    if (cJSON_IsNumber(item)) {
        cfg->match_poll_interval_ms = (uint32_t)item->valuedouble;
        ESP_LOGI(TAG, "Config: match_poll_interval_ms=%lu", (unsigned long)cfg->match_poll_interval_ms);
    }

    item = cJSON_GetObjectItemCaseSensitive(root, "battery_report_interval_ms");
    if (cJSON_IsNumber(item)) {
        cfg->battery_report_interval_ms = (uint32_t)item->valuedouble;
        ESP_LOGI(TAG, "Config: battery_report_interval_ms=%lu", (unsigned long)cfg->battery_report_interval_ms);
    }

    item = cJSON_GetObjectItemCaseSensitive(root, "power_loop_interval_ms");
    if (cJSON_IsNumber(item)) {
        cfg->power_loop_interval_ms = (uint32_t)item->valuedouble;
        ESP_LOGI(TAG, "Config: power_loop_interval_ms=%lu", (unsigned long)cfg->power_loop_interval_ms);
    }

    item = cJSON_GetObjectItemCaseSensitive(root, "failed_attempt_threshold");
    if (cJSON_IsNumber(item)) {
        cfg->failed_attempt_threshold = (uint32_t)item->valuedouble;
        ESP_LOGI(TAG, "Config: failed_attempt_threshold=%lu", (unsigned long)cfg->failed_attempt_threshold);
    }

    item = cJSON_GetObjectItemCaseSensitive(root, "failed_attempt_window_ms");
    if (cJSON_IsNumber(item)) {
        cfg->failed_attempt_window_ms = (uint32_t)item->valuedouble;
        ESP_LOGI(TAG, "Config: failed_attempt_window_ms=%lu", (unsigned long)cfg->failed_attempt_window_ms);
    }

    item = cJSON_GetObjectItemCaseSensitive(root, "lockout_duration_ms");
    if (cJSON_IsNumber(item)) {
        cfg->lockout_duration_ms = (uint32_t)item->valuedouble;
        ESP_LOGI(TAG, "Config: lockout_duration_ms=%lu", (unsigned long)cfg->lockout_duration_ms);
    }

    cJSON_Delete(root);

    // Send confirmation notify with current config subset
    // This will be handled by the caller after the callback returns
}

static void sdf_ble_companion_on_enroll_write(void *ctx,
                                               const uint8_t *data,
                                               size_t len) {
    (void)ctx;
    ESP_LOGI(TAG, "BLE Companion: Enroll write, len=%u", (unsigned)len);

    cJSON *root = cJSON_ParseWithLength((const char *)data, len);
    if (!root) {
        ESP_LOGW(TAG, "Enroll write: invalid JSON");
        return;
    }

    cJSON *user_id_item = cJSON_GetObjectItemCaseSensitive(root, "user_id");
    cJSON *permission_item = cJSON_GetObjectItemCaseSensitive(root, "permission");

    if (!cJSON_IsNumber(user_id_item) || !cJSON_IsNumber(permission_item)) {
        ESP_LOGW(TAG, "Enroll write: missing or invalid user_id/permission");
        cJSON_Delete(root);
        return;
    }

    uint16_t user_id = (uint16_t)user_id_item->valuedouble;
    uint8_t permission = (uint8_t)permission_item->valuedouble;

    if (user_id == 0 || user_id > SDF_FINGERPRINT_USER_ID_MAX) {
        ESP_LOGW(TAG, "Enroll write: user_id out of range (1-%d)", SDF_FINGERPRINT_USER_ID_MAX);
        cJSON_Delete(root);
        return;
    }

    if (permission < 1 || permission > 3) {
        ESP_LOGW(TAG, "Enroll write: permission out of range (1-3)");
        cJSON_Delete(root);
        return;
    }

    esp_err_t err = sdf_services_request_enrollment(user_id, permission);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to request enrollment: %s", esp_err_to_name(err));
    }

    cJSON_Delete(root);
}

static void sdf_ble_companion_on_ota_write(void *ctx,
                                            const uint8_t *data,
                                            size_t len) {
    (void)ctx;
    esp_err_t err = sdf_ble_companion_start_ota_request(data, len);
    if (err != ESP_OK) {
      ESP_LOGW(TAG, "BLE Companion OTA request rejected: %s",
               esp_err_to_name(err));
    }
}

static void sdf_app_on_event(void *ctx, const sdf_event_router_event_t *event) {
  (void)ctx;
  if (event == NULL) {
    return;
  }

  switch (event->type) {
  case SDF_EVENT_ROUTER_BIOMETRIC_MATCH: {
    ESP_LOGI(TAG, "Event: Biometric match user_id=%u", (unsigned)event->payload.biometric.user_id);
    if (!s_has_creds || s_pairing_active) {
      return;
    }
    int percent = sdf_drivers_battery_get_percent();
    if (percent <= 20) {
      sdf_services_trigger_low_battery_warning();
    }
    sdf_app_lock_action(SDF_LOCK_ACTION_UNLATCH, 0);
    break;
  }
  case SDF_EVENT_ROUTER_SECURITY_LOCKOUT: {
    if (event->payload.security.failed_attempts > 0) {
      sdf_app_set_alarm_mask_bits(SDF_APP_ZB_ALARM_BIOMETRIC_LOCKOUT, 0);
      sdf_app_emit_audit(SDF_AUDIT_BIOMETRIC_LOCKOUT, 0,
                         (int32_t)event->payload.security.failed_attempts,
                         (uint16_t)event->payload.security.failed_attempts);
    } else {
      sdf_app_set_alarm_mask_bits(0, SDF_APP_ZB_ALARM_BIOMETRIC_LOCKOUT);
      sdf_app_emit_audit(SDF_AUDIT_BIOMETRIC_LOCKOUT_CLEARED, 0, 0, 0);
    }
    break;
  }
  case SDF_EVENT_ROUTER_BIOMETRIC_MATCH_FAILED: {
    sdf_app_emit_audit(SDF_AUDIT_BIOMETRIC_FAILED, 0,
                       (int32_t)event->payload.security.failed_attempts,
                       (uint16_t)event->payload.security.failed_attempts);
    break;
  }
  case SDF_EVENT_ROUTER_ENROLLMENT_STEP_COMPLETE: {
    sdf_power_mark_activity();
    ESP_LOGI(TAG, "Event: Enrollment step=%u status=%u",
             (unsigned)event->payload.enrollment.step,
             (unsigned)event->payload.enrollment.status);
    if (event->payload.enrollment.status == SDF_ENROLLMENT_STATE_SUCCESS) {
      sdf_app_set_alarm_mask_bits(0, SDF_APP_ZB_ALARM_ACTION_FAILURE);
    } else if (event->payload.enrollment.status == SDF_ENROLLMENT_STATE_ERROR) {
      sdf_app_set_alarm_mask_bits(SDF_APP_ZB_ALARM_ACTION_FAILURE, 0);
    }
    break;
  }
  case SDF_EVENT_ROUTER_ENROLLMENT_COMPLETE: {
    sdf_power_mark_activity();
    ESP_LOGI(TAG, "Event: Enrollment COMPLETE user_id=%u",
             (unsigned)event->payload.enrollment_complete.user_id);
    sdf_app_set_alarm_mask_bits(0, SDF_APP_ZB_ALARM_ACTION_FAILURE);
    break;
  }
  case SDF_EVENT_ROUTER_ENROLLMENT_FAILED: {
    sdf_power_mark_activity();
    ESP_LOGW(TAG, "Event: Enrollment FAILED step=%u error=%d",
             (unsigned)event->payload.enrollment_failed.step,
             (int)event->payload.enrollment_failed.error_code);
    sdf_app_set_alarm_mask_bits(SDF_APP_ZB_ALARM_ACTION_FAILURE, 0);
    sdf_app_emit_audit(SDF_AUDIT_BIOMETRIC_FAILED, 0,
                       (int32_t)event->payload.enrollment_failed.error_code, 0);
    led_flash_red();
    break;
  }
  case SDF_EVENT_ROUTER_AUDIT: {
    ESP_LOGI(TAG, "AUDIT %s user=%u status=%ld detail=%u",
             sdf_app_audit_event_name(event->payload.audit.type),
             (unsigned)event->payload.audit.user_id,
             event->payload.audit.status,
             (unsigned)event->payload.audit.detail);
    switch (event->payload.audit.type) {
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
    break;
  }
  case SDF_EVENT_ROUTER_WEB_REG_AUTH_REQUEST: {
    sdf_app_on_web_reg_auth_request(ctx, event);
    break;
  }
  case SDF_EVENT_ROUTER_WEB_REG_AUTH_RESULT: {
    sdf_app_on_web_reg_auth_result(ctx, event);
    break;
  }
  default:
    break;
  }
}

static void sdf_app_on_web_reg_auth_request(void *ctx,
                                             const sdf_event_router_event_t *event) {
  (void)ctx;
  if (!event) {
    return;
  }

  ESP_LOGI(TAG, "Web registration auth request for user: %s",
           event->payload.web_reg_auth_request.username);

  sdf_services_set_web_reg_auth(event->payload.web_reg_auth_request.username,
                                 event->payload.web_reg_auth_request.password_hash,
                                 SDF_STORAGE_WEB_USER_HASH_LEN);

  sdf_services_request_admin_action(SDF_SERVICES_ADMIN_ACTION_WEB_REG_AUTH);
}

static void sdf_app_on_web_reg_auth_result(void *ctx,
                                             const sdf_event_router_event_t *event) {
  (void)ctx;
  if (!event) {
    return;
  }

  ESP_LOGI(TAG, "Web registration auth result: %s for user: %s",
           event->payload.web_reg_auth_result.authorized ? "AUTHORIZED" : "DENIED",
           event->payload.web_reg_auth_result.username);

  if (event->payload.web_reg_auth_result.authorized) {
    sdf_storage_web_user_t user = {0};
    strncpy(user.username, event->payload.web_reg_auth_result.username,
            SDF_STORAGE_WEB_USER_NAME_MAX - 1);
    user.username[SDF_STORAGE_WEB_USER_NAME_MAX - 1] = '\0';
    user.permission = event->payload.web_reg_auth_result.permission;
    user.valid = true;

    uint8_t password_hash[SDF_STORAGE_WEB_USER_HASH_LEN];
    if (sdf_services_get_web_reg_auth(NULL, 0, NULL) == ESP_OK) {
      // Get the stored password hash
      sdf_services_get_web_reg_password_hash(password_hash, SDF_STORAGE_WEB_USER_HASH_LEN);
      memcpy(user.password_hash, password_hash, SDF_STORAGE_WEB_USER_HASH_LEN);
    }

    for (uint8_t i = 0; i < SDF_STORAGE_WEB_USER_MAX; i++) {
      sdf_storage_web_user_t existing;
      if (sdf_storage_web_user_load(i, &existing) == ESP_OK && !existing.valid) {
        sdf_storage_web_user_save(i, &user);
        ESP_LOGI(TAG, "Saved web user at index %u", (unsigned)i);
        break;
      }
    }
  }

  sdf_ble_companion_reply_auth(event->payload.web_reg_auth_result.username, 
                               event->payload.web_reg_auth_result.authorized);

  sdf_services_clear_web_reg_auth();
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
  if (!sdf_protocol_zigbee_is_enabled()) {
    ESP_LOGI(TAG, "Skipping Zigbee user sync because Zigbee is disabled");
    return;
  }

  const size_t max_users = (size_t)SDF_FINGERPRINT_USER_ID_MAX + 1u;
  uint16_t user_ids[11];
  uint8_t perms[11];
  size_t count = 0;

  esp_err_t err = sdf_services_query_users(user_ids, perms, &count, max_users);
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

void sdf_app_emit_audit(sdf_audit_event_type_t type, uint16_t user_id,
                                int32_t status, uint16_t detail) {
  sdf_event_router_event_t event = {
      .type = SDF_EVENT_ROUTER_AUDIT,
      .timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL),
      .priority = SDF_EVENT_ROUTER_PRIO_NORMAL,
      .payload.audit.type = type,
      .payload.audit.user_id = user_id,
      .payload.audit.status = status,
      .payload.audit.detail = detail,
  };
  sdf_event_router_emit(&event);
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
  sdf_app_release_ble_transport("lock action failed");
}

static void sdf_app_lf_on_progress(void *ctx, bool in_progress, uint8_t action,
                                   uint8_t retries) {
  (void)ctx;
  (void)in_progress;
  (void)action;
  (void)retries;
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
  if (!s_has_creds || s_pairing_active) {
    return SDF_NUKI_RESULT_ERR_ARG;
  }
  if (!sdf_nuki_ble_is_ready(&s_ble)) {
    sdf_app_resume_ble_transport("keyturner state request while disconnected");
    return SDF_NUKI_RESULT_ERR_INCOMPLETE;
  }

  return sdf_nuki_client_send_request_data(
      &s_client, SDF_NUKI_CMD_KEYTURNER_STATES, NULL, 0);
}

int sdf_app_lock_action(uint8_t lock_action, uint8_t flags) {
  sdf_power_mark_activity();
  if (!s_has_creds || s_pairing_active) {
    return SDF_NUKI_RESULT_ERR_ARG;
  }

  if (!sdf_app_valid_lock_action(lock_action)) {
    return SDF_NUKI_RESULT_ERR_ARG;
  }

  if (!sdf_nuki_ble_is_ready(&s_ble)) {
    return sdf_app_queue_lock_action(lock_action, flags);
  }

  return sdf_lock_flow_begin(&s_lock_flow, lock_action, flags);
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

    sdf_protocol_zigbee_update_lock_state(
        sdf_app_map_lock_state_to_zigbee(state.lock_state));
    if (state.critical_battery_state) {
      sdf_app_set_alarm_mask_bits(SDF_APP_ZB_ALARM_LOW_BATTERY, 0);
    } else {
      sdf_app_set_alarm_mask_bits(0, SDF_APP_ZB_ALARM_LOW_BATTERY);
    }
    sdf_app_release_ble_transport("keyturner state synchronized");
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

    sdf_app_set_alarm_mask_bits(SDF_APP_ZB_ALARM_ACTION_FAILURE, 0);

    sdf_lock_flow_on_error(&s_lock_flow);
    return;
  }

  ESP_LOGI(TAG, "Nuki message cmd=0x%04X len=%u", msg->command_id,
           (unsigned)msg->payload_len);
}

static void sdf_app_start_requested_nuki_pairing(void) {
  if (!s_pairing_requested || s_pairing_active) {
    return;
  }

  if (!sdf_nuki_ble_is_ready(&s_ble)) {
    return;
  }

  if (s_has_creds) {
    ESP_LOGW(TAG,
             "Starting Nuki pairing with existing stored credentials; "
             "successful pairing will replace them");
  }

  int res = sdf_nuki_pairing_init(&s_pairing, &s_client, 1 /* Bridge */, SDF_APP_ID,
                                  SDF_APP_NAME);
  if (res != SDF_NUKI_RESULT_OK) {
    ESP_LOGE(TAG, "Pairing init failed: %d", res);
    sdf_app_emit_audit(SDF_AUDIT_PAIRING_FAILED, 0, res, 0);
    s_pairing_requested = false;
    led_flash_red();
    return;
  }

  s_pairing_active = true;
  s_pairing_requested = false;
  res = sdf_nuki_pairing_start(&s_pairing);
  if (res != SDF_NUKI_RESULT_OK) {
    ESP_LOGE(TAG, "Pairing start failed: %d", res);
    sdf_app_emit_audit(SDF_AUDIT_PAIRING_FAILED, 0, res, 1);
    s_pairing_active = false;
    led_flash_red();
  }
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

  if (s_pairing_requested) {
    ESP_LOGI(TAG, "BLE transport ready; starting requested Nuki pairing");
    sdf_app_start_requested_nuki_pairing();
    if (s_pairing_active) {
      return;
    }
  }

  if (!s_has_creds) {
    ESP_LOGI(TAG,
             "No Nuki credentials found. Waiting for Admin Action to pair.");
    return;
  }

  if (s_lock_action_pending) {
    int res = sdf_app_dispatch_pending_lock_action();
    if (res != SDF_NUKI_RESULT_OK) {
      ESP_LOGW(TAG, "Queued lock action still waiting after BLE ready: %d", res);
    }
    return;
  }

  int res = sdf_app_request_keyturner_state();
  if (res != SDF_NUKI_RESULT_OK) {
    ESP_LOGW(TAG, "Initial keyturner state request failed: %d", res);
  }
}

static void sdf_app_check_pairing_complete(void) {
  if (!s_pairing_active || s_pairing.state != SDF_NUKI_PAIRING_COMPLETE) {
    return;
  }

  sdf_nuki_credentials_t creds;
  if (sdf_nuki_pairing_get_credentials(&s_pairing, &creds) !=
      SDF_NUKI_RESULT_OK) {
    return;
  }

  s_client.creds = creds;
  s_has_creds = true;
  s_pairing_active = false;
  sdf_app_emit_audit(SDF_AUDIT_PAIRING_COMPLETE, 0,
                     (int32_t)creds.authorization_id, 0);
  esp_err_t err = sdf_storage_nuki_save(creds.authorization_id, creds.shared_key);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to save credentials: %s", esp_err_to_name(err));
    sdf_app_emit_audit(SDF_AUDIT_STORAGE_POLICY_FAILED, 0, err, 1);
  }
  ESP_LOGI(TAG, "Pairing complete; credentials stored");
  led_solid_green();
  int res = sdf_app_request_keyturner_state();
  if (res != SDF_NUKI_RESULT_OK) {
    ESP_LOGW(TAG, "Post-pair keyturner state request failed: %d", res);
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
        led_flash_red();
      }

      sdf_app_check_pairing_complete();
    }
    return;
  }

  if (s_pairing_active) {
    int pairing_res = sdf_nuki_pairing_handle_encrypted(&s_pairing, data, len);
    if (pairing_res != SDF_NUKI_RESULT_OK &&
        pairing_res != SDF_NUKI_RESULT_ERR_INCOMPLETE) {
      ESP_LOGW(TAG, "Pairing USDIO handling failed: %d", pairing_res);
      sdf_app_emit_audit(SDF_AUDIT_PAIRING_FAILED, 0, pairing_res, 3);
      led_flash_red();
    }

    sdf_app_check_pairing_complete();
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

esp_err_t sdf_app_init(void) {
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
  s_lock_action_pending = false;
  s_pending_lock_action = 0;
  s_pending_lock_flags = 0;

  esp_err_t err = sdf_storage_init();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Storage init failed: %s", esp_err_to_name(err));
    sdf_app_emit_audit(SDF_AUDIT_STORAGE_POLICY_FAILED, 0, err, 0);
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

  const sdf_config_t *cfg = sdf_config_get();

  // Initialize event router
  err = sdf_event_router_init();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize event router: %s", esp_err_to_name(err));
    return err;
  }

  // Subscribe to events
  sdf_event_router_subscriber_t *subs[9];
  size_t subs_count = 0;

  err = sdf_event_router_subscribe(SDF_EVENT_ROUTER_BIOMETRIC_MATCH,
                                        SDF_EVENT_ROUTER_PRIO_HIGH,
                                        sdf_app_on_event, NULL, &subs[subs_count]);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to subscribe to biometric match: %s", esp_err_to_name(err));
    goto sub_cleanup;
  }
  subs_count++;

  err = sdf_event_router_subscribe(SDF_EVENT_ROUTER_SECURITY_LOCKOUT,
                                        SDF_EVENT_ROUTER_PRIO_CRITICAL,
                                        sdf_app_on_event, NULL, &subs[subs_count]);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to subscribe to security lockout: %s", esp_err_to_name(err));
    goto sub_cleanup;
  }
  subs_count++;

  err = sdf_event_router_subscribe(SDF_EVENT_ROUTER_BIOMETRIC_MATCH_FAILED,
                                        SDF_EVENT_ROUTER_PRIO_HIGH,
                                        sdf_app_on_event, NULL, &subs[subs_count]);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to subscribe to biometric match failed: %s", esp_err_to_name(err));
    goto sub_cleanup;
  }
  subs_count++;

  err = sdf_event_router_subscribe(SDF_EVENT_ROUTER_ENROLLMENT_STEP_COMPLETE,
                                        SDF_EVENT_ROUTER_PRIO_NORMAL,
                                        sdf_app_on_event, NULL, &subs[subs_count]);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to subscribe to enrollment step: %s", esp_err_to_name(err));
    goto sub_cleanup;
  }
  subs_count++;

  err = sdf_event_router_subscribe(SDF_EVENT_ROUTER_ENROLLMENT_COMPLETE,
                                        SDF_EVENT_ROUTER_PRIO_NORMAL,
                                        sdf_app_on_event, NULL, &subs[subs_count]);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to subscribe to enrollment complete: %s", esp_err_to_name(err));
    goto sub_cleanup;
  }
  subs_count++;

  err = sdf_event_router_subscribe(SDF_EVENT_ROUTER_ENROLLMENT_FAILED,
                                        SDF_EVENT_ROUTER_PRIO_NORMAL,
                                        sdf_app_on_event, NULL, &subs[subs_count]);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to subscribe to enrollment failed: %s", esp_err_to_name(err));
    goto sub_cleanup;
  }
  subs_count++;

  err = sdf_event_router_subscribe(SDF_EVENT_ROUTER_AUDIT,
                                        SDF_EVENT_ROUTER_PRIO_NORMAL,
                                        sdf_app_on_event, NULL, &subs[subs_count]);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to subscribe to audit: %s", esp_err_to_name(err));
    goto sub_cleanup;
  }
  subs_count++;

  err = sdf_event_router_subscribe(SDF_EVENT_ROUTER_WEB_REG_AUTH_REQUEST,
                                        SDF_EVENT_ROUTER_PRIO_HIGH,
                                        sdf_app_on_web_reg_auth_request, NULL, &subs[subs_count]);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to subscribe to web reg auth request: %s", esp_err_to_name(err));
    goto sub_cleanup;
  }
  subs_count++;

  err = sdf_event_router_subscribe(SDF_EVENT_ROUTER_WEB_REG_AUTH_RESULT,
                                        SDF_EVENT_ROUTER_PRIO_HIGH,
                                        sdf_app_on_web_reg_auth_result, NULL, &subs[subs_count]);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to subscribe to web reg auth result: %s", esp_err_to_name(err));
    goto sub_cleanup;
  }
  subs_count++;

  goto sub_done;

sub_cleanup:
  while (subs_count > 0) {
    subs_count--;
    sdf_event_router_unsubscribe(subs[subs_count]);
  }
  return err;

sub_done:

  sdf_services_config_t services_cfg;
  sdf_services_get_default_config(&services_cfg);
  services_cfg.admin_action_cb = sdf_app_on_admin_action;
  services_cfg.admin_action_ctx = NULL;

  err = sdf_services_init(&services_cfg);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Failed to initialize fingerprint services: %s",
             esp_err_to_name(err));
  }

  sdf_ble_companion_callbacks_t ble_companion_cbs = {
      .ctx = NULL,
      .on_auth_request = sdf_ble_companion_on_auth_request,
      .on_config_write = sdf_ble_companion_on_config_write,
      .on_enroll_write = sdf_ble_companion_on_enroll_write,
      .on_ota_write = sdf_ble_companion_on_ota_write,
  };
  err = sdf_ble_companion_init(&ble_companion_cbs);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Failed to initialize BLE companion service: %s",
             esp_err_to_name(err));
  }

  if (sdf_protocol_zigbee_is_enabled()) {
    err = sdf_protocol_zigbee_set_checkin_interval_ms(cfg->checkin_interval_ms);
    if (err != ESP_OK) {
      ESP_LOGW(TAG, "Failed to set Zigbee check-in interval: %s",
               esp_err_to_name(err));
    }

    err = sdf_protocol_zigbee_set_command_handler(sdf_app_on_zigbee_command,
                                                 NULL);
    if (err != ESP_OK) {
      ESP_LOGW(TAG, "Failed to set Zigbee command handler: %s",
               esp_err_to_name(err));
    }

    err = sdf_protocol_zigbee_init();
    if (err != ESP_OK) {
      ESP_LOGW(TAG, "Failed to start Zigbee protocol: %s",
               esp_err_to_name(err));
    } else {
      sdf_protocol_zigbee_update_lock_state(
          SDF_PROTOCOL_ZIGBEE_LOCK_STATE_UNDEFINED);
      sdf_app_update_battery_percent(cfg->battery_default_percent);
      sdf_protocol_zigbee_update_alarm_mask(0);
    }
  } else {
    ESP_LOGI(TAG, "Zigbee functionality disabled by configuration");
  }

  sdf_nuki_ble_init(&s_ble, sdf_app_on_ble_rx, NULL, sdf_app_on_ble_ready,
                    NULL);

  /* Load BLE target address from NVS, fall back to config default */
  ble_addr_t ble_target = {
      .type = cfg->nuki_target_addr_type,
      .val = {cfg->nuki_target_addr[0], cfg->nuki_target_addr[1], cfg->nuki_target_addr[2],
              cfg->nuki_target_addr[3], cfg->nuki_target_addr[4], cfg->nuki_target_addr[5]}
  };
  {
    uint8_t stored_type = 0;
    uint8_t stored_addr[6] = {0};
    if (sdf_storage_ble_target_load(&stored_type, stored_addr) == ESP_OK) {
      ble_target.type = stored_type;
      memcpy(ble_target.val, stored_addr, sizeof(ble_target.val));
      ESP_LOGI(TAG, "Loaded BLE target address from NVS");
    } else {
      if (sdf_nuki_ble_addr_is_empty(&ble_target)) {
        ESP_LOGI(TAG,
                 "No BLE target address configured; using advertisement "
                 "discovery");
      } else {
        ESP_LOGI(TAG, "Using compile-time BLE target address");
      }
    }
  }
if (!sdf_nuki_ble_addr_is_empty(&ble_target)) {
    sdf_nuki_ble_set_target_addr(&s_ble, &ble_target);
    if (cfg->ble_connect_on_demand) {
      ESP_LOGI(TAG,
               "BLE target address configured; connect-on-demand mode defers "
               "initial scan");
    } else {
      ESP_LOGI(TAG, "Starting BLE scan (target address set)");
      sdf_nuki_ble_start(&s_ble);
    }
  } else {
    ESP_LOGI(TAG,
             "No BLE target address; scan deferred until pairing requested");
  }

  sdf_power_manager_config_t power_cfg = {
      .ble_transport = &s_ble,
      .fingerprint_wake_gpio = (gpio_num_t)cfg->fp_wake_gpio,
      .checkin_interval_ms = cfg->checkin_interval_ms,
      .idle_before_sleep_ms = cfg->idle_before_sleep_ms,
      .post_wake_guard_ms = cfg->post_wake_guard_ms,
      .loop_interval_ms = cfg->power_loop_interval_ms,
      .battery_report_interval_ms = cfg->battery_report_interval_ms,
      .enable_light_sleep = cfg->enable_light_sleep,
      .enable_ble_radio_gating = cfg->enable_ble_radio_gating,
      .enable_deep_sleep_fallback = cfg->enable_deep_sleep_fallback,
      .battery_percent_default = cfg->battery_default_percent,
      .busy_cb = sdf_app_power_busy,
      .busy_ctx = NULL,
      .wake_cb = sdf_app_power_wakeup,
      .wake_ctx = NULL,
      .battery_cb = sdf_app_power_battery_percent,
      .battery_ctx = NULL,
  };

  err = sdf_power_init_power_manager(&power_cfg);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Failed to start power manager: %s", esp_err_to_name(err));
  }

  err = sdf_cli_init();
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Failed to initialize CLI: %s", esp_err_to_name(err));
  }

  sdf_power_mark_activity();

  return ESP_OK;
}

sdf_nuki_ble_transport_t *sdf_app_get_ble_transport(void) {
  return &s_ble;
}

sdf_nuki_client_t *sdf_app_get_nuki_client(void) {
  return &s_client;
}

sdf_nuki_pairing_t *sdf_app_get_nuki_pairing(void) {
  return &s_pairing;
}

#ifdef SDF_APP_TESTING
#include "sdf_app_test_exports.inc"
#endif
