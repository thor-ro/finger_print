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
static bool s_periodic_poll_pending;

/* BLE-triggered admin action requests (Nuki re-pair, Enroll-Admin, Zigbee
 * Join): tracks the originating connection and which action it requested,
 * so the eventual admin-fingerprint approval/denial/timeout result (which
 * arrives asynchronously with no connection context of its own - see
 * SDF_EVENT_ROUTER_ADMIN_ACTION_COMPLETE's payload) can be routed back to
 * the right BLE client. Shared across all three action types rather than
 * duplicated per type: sdf_services enforces only one admin action can be
 * pending at a time (see sdf_services_request_admin_action()), so a single
 * pending+action+handle triple is always enough to describe "the"
 * outstanding BLE-triggered request, if any. */
static bool s_ble_admin_action_pending;
static sdf_services_admin_action_t s_ble_admin_action;
static uint16_t s_ble_admin_action_conn_handle;

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
static void sdf_app_on_admin_action_complete(void *ctx,
                                              const sdf_event_router_event_t *event);
static void sdf_app_on_ble_admin_action_request(void *ctx,
                                                 sdf_services_admin_action_t action,
                                                 uint16_t conn_handle);

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
      !s_pairing_active) {
    if (sdf_nuki_ble_is_ready(&s_ble)) {
      int res = sdf_app_request_keyturner_state();
      if (res != SDF_NUKI_RESULT_OK) {
        ESP_LOGD(TAG, "Periodic keyturner refresh skipped: %d", res);
      }
    } else {
      s_periodic_poll_pending = true;
      sdf_app_resume_ble_transport("periodic state poll");
    }
  }
}

static int sdf_app_power_battery_percent(void *ctx) {
  (void)ctx;
  return sdf_drivers_battery_get_percent();
}

/* Shared by the button-triggered NUKI_PAIR admin action and the
 * BLE-triggered NUKI_REPAIR admin action - both authorize the exact same
 * underlying pairing start, only how the request originated (and how its
 * result is reported back) differs. Returns false without changing any
 * pairing state if pairing is already active/requested. */
static bool sdf_app_execute_nuki_pairing_start(void) {
  if (s_pairing_active || s_pairing_requested) {
    ESP_LOGW(TAG, "Cannot start Nuki pairing: pairing already active");
    led_flash_red();
    return false;
  }

  s_pairing_requested = true;
  led_rapid_yellow();
  sdf_nuki_ble_set_enabled(&s_ble, true);
  sdf_nuki_ble_start(&s_ble);
  if (!sdf_nuki_ble_is_ready(&s_ble)) {
    ESP_LOGI(TAG, "Nuki pairing requested; waiting for BLE transport ready");
  }
  sdf_app_start_requested_nuki_pairing();
  return true;
}

static void sdf_app_on_admin_action(void *ctx,
                                    sdf_services_admin_action_t action) {
  (void)ctx;
  switch (action) {
  case SDF_SERVICES_ADMIN_ACTION_NUKI_PAIR:
    ESP_LOGI(TAG, "Admin authorized Nuki Pairing");
    sdf_power_mark_activity();
    sdf_app_execute_nuki_pairing_start();
    break;

  case SDF_SERVICES_ADMIN_ACTION_NUKI_REPAIR: {
    ESP_LOGI(TAG, "Admin authorized Nuki re-pair (BLE-triggered)");
    sdf_power_mark_activity();

    uint16_t conn_handle = s_ble_admin_action_conn_handle;
    bool was_pending = s_ble_admin_action_pending &&
                        s_ble_admin_action == SDF_SERVICES_ADMIN_ACTION_NUKI_REPAIR;
    if (was_pending) {
      s_ble_admin_action_pending = false;
    }

    bool started = sdf_app_execute_nuki_pairing_start();
    if (was_pending) {
      /* Reply is the "pairing has started" notification per the
       * ble-companion-service delta spec's "Nuki re-pair authorized"
       * scenario; `started == false` here means pairing was already active
       * for another reason, so the client is told denied rather than left
       * to assume success. */
      sdf_ble_companion_reply_admin_action(conn_handle, action, started);
    }
    break;
  }

  case SDF_SERVICES_ADMIN_ACTION_ENROLL_ADMIN: {
    ESP_LOGI(TAG, "Admin authorized Enroll-Admin (BLE-triggered)");
    sdf_power_mark_activity();

    /* The actual enrollment side effect (sdf_services_start_local_enrollment_
     * with_permission(3u)) already ran inside sdf_services_execute_admin_
     * action() before this callback fires - this only routes the "request
     * accepted, enrollment starting" reply back to the requesting BLE
     * client, mirroring NUKI_REPAIR above. Button-triggered ENROLL_ADMIN
     * requests no longer exist (see button-admin-actions-to-companion-app),
     * so this callback only ever fires for a BLE-originated request. */
    if (s_ble_admin_action_pending &&
        s_ble_admin_action == SDF_SERVICES_ADMIN_ACTION_ENROLL_ADMIN) {
      uint16_t conn_handle = s_ble_admin_action_conn_handle;
      s_ble_admin_action_pending = false;
      sdf_ble_companion_reply_admin_action(conn_handle, action, true);
    }
    break;
  }

  case SDF_SERVICES_ADMIN_ACTION_ZB_JOIN: {
    ESP_LOGI(TAG, "Admin authorized Zigbee Join");
    led_rapid_purple();
    esp_err_t err = sdf_protocol_zigbee_permit_join();
    if (err == ESP_ERR_NOT_SUPPORTED) {
      ESP_LOGI(TAG, "Ignoring Zigbee Join request because Zigbee is disabled");
    }

    /* ZB_JOIN is BLE-only as of this change (its Hold-3s button gesture was
     * removed - see sdf_services_button.c), so this callback only ever fires
     * for a BLE-originated request today; guarded on s_ble_admin_action
     * anyway for symmetry with the other two cases and in case a future
     * change reintroduces a button path. */
    if (s_ble_admin_action_pending &&
        s_ble_admin_action == SDF_SERVICES_ADMIN_ACTION_ZB_JOIN) {
      uint16_t conn_handle = s_ble_admin_action_conn_handle;
      s_ble_admin_action_pending = false;
      sdf_ble_companion_reply_admin_action(conn_handle, action, err == ESP_OK);
    }
    break;
  }

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

/* Fired by sdf_ble_companion when an authenticated GATT client writes a
 * `{"action":"nuki_repair"|"enroll_admin"|"zb_join"}` Config request
 * (already rejected by sdf_ble_companion itself, before this is ever called,
 * if the connection isn't authenticated, or - for nuki_repair specifically -
 * if setup isn't complete). Routes into the same admin-fingerprint
 * pending-action flow WEB_REG_AUTH uses; the result is resolved either in
 * sdf_app_on_admin_action() (approval) or sdf_app_on_admin_action_complete()
 * (denial/timeout) via s_ble_admin_action_conn_handle. */
static void sdf_app_on_ble_admin_action_request(void *ctx,
                                                 sdf_services_admin_action_t action,
                                                 uint16_t conn_handle) {
    (void)ctx;
    ESP_LOGI(TAG, "BLE Companion: admin action %d request, conn_handle=%u",
             (int)action, (unsigned)conn_handle);

    s_ble_admin_action_conn_handle = conn_handle;
    s_ble_admin_action = action;
    s_ble_admin_action_pending = true;

    esp_err_t err = sdf_services_request_admin_action(action);
    if (err != ESP_OK) {
        /* Another admin action is already pending (button press, WEB_REG_AUTH,
         * ...) - this request never entered the pending state, so it must be
         * resolved here rather than left for sdf_app_on_admin_action_complete(),
         * which only fires for actions that *did* become pending. */
        ESP_LOGW(TAG, "BLE admin action %d request rejected: %s", (int)action,
                 esp_err_to_name(err));
        s_ble_admin_action_pending = false;
        sdf_ble_companion_reply_admin_action(conn_handle, action, false);
    }
}

/* Applies one numeric field from a BLE config write through a validated
 * sdf_config_set_*() setter, logging and counting the outcome. `name` must
 * match the JSON key. Returns true if the field was present and applied
 * (whether or not it changed anything), so the caller can tell "present but
 * rejected" apart from "absent". */
typedef esp_err_t (*sdf_app_config_set_u32_fn)(uint32_t value);

static bool sdf_ble_companion_apply_config_u32(cJSON *root, const char *name,
                                                sdf_app_config_set_u32_fn setter,
                                                bool *any_rejected) {
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, name);
    if (!cJSON_IsNumber(item)) {
        return false;
    }
    if (item->valuedouble < 0) {
        ESP_LOGW(TAG, "Config write: %s rejected (negative)", name);
        *any_rejected = true;
        return true;
    }
    esp_err_t err = setter((uint32_t)item->valuedouble);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Config write: %s=%.0f rejected: %s", name, item->valuedouble,
                 esp_err_to_name(err));
        *any_rejected = true;
    } else {
        ESP_LOGI(TAG, "Config: %s=%lu", name, (unsigned long)item->valuedouble);
    }
    return true;
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

    /* Runtime overrides being disabled (sdf_config_get_mutable() would
     * return NULL) surfaces the same way as any other rejected field: each
     * setter below returns ESP_ERR_INVALID_STATE in that case, so there's
     * no need to special-case it here - every field just gets logged as
     * rejected and any_applied/any_rejected reflect that accurately. */
    bool any_applied = false;
    bool any_rejected = false;

    any_applied |= sdf_ble_companion_apply_config_u32(
        root, "checkin_interval_ms", sdf_config_set_checkin_interval, &any_rejected);
    any_applied |= sdf_ble_companion_apply_config_u32(
        root, "idle_before_sleep_ms", sdf_config_set_idle_before_sleep, &any_rejected);
    any_applied |= sdf_ble_companion_apply_config_u32(
        root, "post_wake_guard_ms", sdf_config_set_post_wake_guard, &any_rejected);
    any_applied |= sdf_ble_companion_apply_config_u32(
        root, "match_poll_interval_ms", sdf_config_set_match_poll_interval, &any_rejected);
    any_applied |= sdf_ble_companion_apply_config_u32(
        root, "battery_report_interval_ms", sdf_config_set_battery_report_interval, &any_rejected);
    any_applied |= sdf_ble_companion_apply_config_u32(
        root, "power_loop_interval_ms", sdf_config_set_power_loop_interval, &any_rejected);
    any_applied |= sdf_ble_companion_apply_config_u32(
        root, "failed_attempt_threshold", sdf_config_set_failed_attempt_threshold, &any_rejected);
    any_applied |= sdf_ble_companion_apply_config_u32(
        root, "failed_attempt_window_ms", sdf_config_set_failed_attempt_window, &any_rejected);
    any_applied |= sdf_ble_companion_apply_config_u32(
        root, "lockout_duration_ms", sdf_config_set_lockout_duration, &any_rejected);

    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, "battery_default_percent");
    if (cJSON_IsNumber(item)) {
        any_applied = true;
        if (item->valuedouble < 0 || item->valuedouble > 255) {
            ESP_LOGW(TAG, "Config write: battery_default_percent rejected (out of range)");
            any_rejected = true;
        } else {
            esp_err_t err = sdf_config_set_battery_default_percent((uint8_t)item->valuedouble);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "Config write: battery_default_percent=%.0f rejected: %s",
                         item->valuedouble, esp_err_to_name(err));
                any_rejected = true;
            } else {
                ESP_LOGI(TAG, "Config: battery_default_percent=%u", (unsigned)item->valuedouble);
            }
        }
    }

    item = cJSON_GetObjectItemCaseSensitive(root, "ble_connect_on_demand");
    if (cJSON_IsBool(item)) {
        any_applied = true;
        esp_err_t err = sdf_config_set_ble_connect_on_demand(cJSON_IsTrue(item));
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Config write: ble_connect_on_demand rejected: %s", esp_err_to_name(err));
            any_rejected = true;
        } else {
            ESP_LOGI(TAG, "Config: ble_connect_on_demand=%d", cJSON_IsTrue(item));
        }
    }

    cJSON_Delete(root);

    if (any_applied && !any_rejected) {
        esp_err_t err = sdf_config_save();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Config write: failed to persist to NVS: %s", esp_err_to_name(err));
        }
    }

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

static void sdf_app_on_event(void *ctx, const sdf_event_router_event_t *event) {
  (void)ctx;
  if (event == NULL) {
    return;
  }

  switch (event->type) {
  case SDF_EVENT_ROUTER_BIOMETRIC_MATCH: {
    uint16_t match_user_id = event->payload.biometric.user_id;
    uint8_t match_permission = event->payload.biometric.permission;
    ESP_LOGI(TAG, "Event: Biometric match user_id=%u permission=%u",
             (unsigned)match_user_id, (unsigned)match_permission);
    if (!s_has_creds || s_pairing_active) {
      return;
    }
    /* Defensive check only: today every valid permission level (1-3, see
     * doc/features.md - Standard/Elevated/Admin) is entitled to unlatch,
     * so this does not implement tiered access control. It refuses to act
     * on a permission value that couldn't have come from a real enrolled
     * user (sdf_services_change_user_permission()/enrollment both reject
     * 0 and >3), which would indicate corrupt/unexpected sensor data.
     * If unlatch should ever be restricted by permission tier, that's a
     * product decision the "Elevated User" placeholder in doc/features.md
     * says is still open - decide it there, not by guessing here. */
    if (match_permission < 1 || match_permission > 3) {
      ESP_LOGE(TAG, "Rejecting unlatch: invalid permission %u for user_id=%u",
               (unsigned)match_permission, (unsigned)match_user_id);
      sdf_app_emit_audit(SDF_AUDIT_BIOMETRIC_FAILED, match_user_id,
                         ESP_ERR_INVALID_STATE, match_permission);
      return;
    }
    int percent = sdf_drivers_battery_get_percent();
    if (percent <= 20) {
      sdf_services_trigger_low_battery_warning();
    }
    sdf_app_emit_audit(SDF_AUDIT_BIOMETRIC_MATCH_SUCCESS, match_user_id, ESP_OK,
                       match_permission);
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

  const char *username = event->payload.web_reg_auth_result.username;
  bool admin_authorized = event->payload.web_reg_auth_result.authorized;

  /* Default: no user decided yet, reply mirrors the admin decision. Only
   * overwritten below once a password hash was successfully fetched, so a
   * hash-fetch failure still replies with the original authorized flag
   * (matching prior behavior) while never persisting a user. */
  sdf_services_web_auth_registration_decision_t decision = {
      .should_persist = false,
      .reply_authorized = admin_authorized,
  };

  if (admin_authorized) {
    uint8_t password_hash[SDF_STORAGE_WEB_USER_HASH_LEN];
    esp_err_t hash_err = sdf_services_get_web_reg_password_hash(
        password_hash, SDF_STORAGE_WEB_USER_HASH_LEN);
    if (hash_err != ESP_OK) {
      /* No pending request to pull the hash from (e.g. it was already
       * cleared) - do not persist a user with a zeroed-out credential. */
      ESP_LOGE(TAG, "Failed to fetch web reg password hash: %s",
               esp_err_to_name(hash_err));
    } else {
      decision = sdf_services_web_auth_decide_registration(
          username, password_hash, SDF_STORAGE_WEB_USER_HASH_LEN,
          event->payload.web_reg_auth_result.permission, true);
    }
  }

  if (decision.should_persist) {
    for (uint8_t i = 0; i < SDF_STORAGE_WEB_USER_MAX; i++) {
      sdf_storage_web_user_t existing;
      if (sdf_storage_web_user_load(i, &existing) == ESP_OK && !existing.valid) {
        sdf_storage_web_user_save(i, &decision.user);
        ESP_LOGI(TAG, "Saved web user at index %u", (unsigned)i);
        break;
      }
    }
  }

  sdf_ble_companion_reply_auth(username, decision.reply_authorized);

  sdf_services_clear_web_reg_auth();
}

/* Handles the non-success side of a WEB_REG_AUTH admin action: a timeout
 * (no admin fingerprint presented within SDF_ADMIN_ACTION_TIMEOUT_MS) or a
 * rejection (a non-admin fingerprint claimed the pending action instead).
 * Either way sdf_admin_task only emits ADMIN_ACTION_COMPLETE with a
 * non-OK result - never WEB_REG_AUTH_RESULT - so this is the only place
 * that resolves the request in those cases. Without it the BLE client
 * waiting on the auth characteristic would never be notified, and
 * s_state.web_reg_auth_pending would stay latched, permanently blocking
 * any subsequent web registration request. */
static void sdf_app_on_admin_action_complete(void *ctx,
                                              const sdf_event_router_event_t *event) {
  (void)ctx;
  if (!event) {
    return;
  }

  sdf_services_admin_action_t action = event->payload.admin_action_complete.action;
  esp_err_t result = event->payload.admin_action_complete.result;

  if (sdf_services_web_auth_should_resolve_on_action_complete(action, result)) {
    char username[SDF_STORAGE_WEB_USER_NAME_MAX];
    uint8_t permission = 0;
    esp_err_t err = sdf_services_get_web_reg_auth(username, sizeof(username), &permission);
    if (err == ESP_OK) {
      ESP_LOGW(TAG, "Web registration auth for user '%s' not granted: %s",
               username, esp_err_to_name(result));
      sdf_ble_companion_reply_auth(username, false);
      sdf_services_clear_web_reg_auth();
    }
    /* else: already cleared/consumed by another path - nothing to deny. */
  }

  /* Same "always resolve" guarantee for the BLE-triggered actions (Nuki
   * re-pair, Enroll-Admin, Zigbee Join) as above for WEB_REG_AUTH:
   * sdf_admin_task only ever emits ADMIN_ACTION_COMPLETE with a non-OK
   * result on timeout/denial, never an action-specific success/failure
   * event, so this is the only place that resolves the pending BLE client
   * in those cases. Guarded on s_ble_admin_action matching `action` because
   * a *different* admin action could complete (e.g. a button press) while
   * one of these is pending - although sdf_services_request_admin_action()
   * already prevents that from happening concurrently today, this keeps the
   * guard correct even if that single-pending-action invariant ever loosens. */
  if (s_ble_admin_action_pending && s_ble_admin_action == action &&
      sdf_services_ble_admin_action_should_resolve_on_action_complete(action, result)) {
    ESP_LOGW(TAG, "BLE admin action %d request not granted: %s", (int)action,
             esp_err_to_name(result));
    uint16_t conn_handle = s_ble_admin_action_conn_handle;
    s_ble_admin_action_pending = false;
    sdf_ble_companion_reply_admin_action(conn_handle, action, false);
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

  /* Serialize to JSON array: e.g. [{"id":1,"perm":3,"name":"Alice"},{"id":5,"perm":1}] */
  cJSON *root = cJSON_CreateArray();
  if (!root) {
    ESP_LOGE(TAG, "Failed to create JSON array for Zigbee user sync");
    return;
  }

  for (size_t i = 0; i < count; i++) {
    char name[SDF_STORAGE_FP_USER_NAME_MAX];
    err = sdf_storage_load_user_name(user_ids[i], name, sizeof(name));
    if (err != ESP_OK) {
      name[0] = '\0';
    }

    cJSON *user_obj = cJSON_CreateObject();
    if (user_obj) {
      cJSON_AddNumberToObject(user_obj, "id", user_ids[i]);
      cJSON_AddNumberToObject(user_obj, "perm", perms[i]);
      if (name[0] != '\0') {
        cJSON_AddStringToObject(user_obj, "name", name);
      }
      cJSON_AddItemToArray(root, user_obj);
    }
  }

  char *json_str = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);

  if (json_str) {
    sdf_protocol_zigbee_update_user_list(json_str);
    ESP_LOGI(TAG, "Synced active users to Zigbee: %s", json_str);
    free(json_str);
  } else {
    ESP_LOGE(TAG, "Failed to serialize user list JSON");
  }
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

  // Handle periodic state poll if pending
  if (s_periodic_poll_pending) {
    s_periodic_poll_pending = false;
    int res = sdf_app_request_keyturner_state();
    if (res != SDF_NUKI_RESULT_OK) {
      ESP_LOGW(TAG, "Periodic keyturner state request failed: %d", res);
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

  /* Must run after sdf_storage_init() (which brings up NVS): it loads any
   * persisted runtime config overrides via NVS, falling back to Kconfig
   * defaults if there aren't any. Without this call sdf_config_get()/
   * _get_mutable() below would read an all-zero, never-populated struct -
   * every field defaults to whatever sdf_config_t's static zero-initializer
   * gives it, not the CONFIG_SDF_* Kconfig values. */
  err = sdf_config_init();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Config init failed: %s", esp_err_to_name(err));
    return err;
  }


#ifndef CONFIG_IDF_TARGET_LINUX
  esp_task_wdt_config_t twdt_config = {
      .timeout_ms = sdf_config_get()->wdt_timeout_ms,
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
  sdf_event_router_subscriber_t *subs[10];
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

  /* WEB_REG_AUTH_RESULT above is only ever emitted on success (see
   * sdf_services_execute_admin_action()); if the admin fingerprint auth
   * times out or is denied, sdf_admin_task instead emits
   * ADMIN_ACTION_COMPLETE with a non-OK result and nothing else. Without
   * this subscription that left the BLE client's auth request stuck
   * pending forever (no notify ever sent) and s_state.web_reg_auth_pending
   * latched true server-side, blocking any future web reg auth request. */
  err = sdf_event_router_subscribe(SDF_EVENT_ROUTER_ADMIN_ACTION_COMPLETE,
                                        SDF_EVENT_ROUTER_PRIO_HIGH,
                                        sdf_app_on_admin_action_complete, NULL, &subs[subs_count]);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to subscribe to admin action complete: %s", esp_err_to_name(err));
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
  /* Tracks the first subsystem-init failure below so the function can
   * return an honest status while still attempting to bring up every
   * other subsystem (a partially-working door lock beats a hard abort). */
  esp_err_t degraded_err = ESP_OK;

  sdf_services_config_t services_cfg;
  sdf_services_get_default_config(&services_cfg);
  services_cfg.admin_action_cb = sdf_app_on_admin_action;
  services_cfg.admin_action_ctx = NULL;

  err = sdf_services_init(&services_cfg);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Failed to initialize fingerprint services: %s",
             esp_err_to_name(err));
    if (degraded_err == ESP_OK) {
      degraded_err = err;
    }
  }

  sdf_ble_companion_callbacks_t ble_companion_cbs = {
      .ctx = NULL,
      .on_auth_request = sdf_ble_companion_on_auth_request,
      .on_config_write = sdf_ble_companion_on_config_write,
      .on_enroll_write = sdf_ble_companion_on_enroll_write,
      .on_ota_write = sdf_ble_companion_handle_ota_write,
      .on_admin_action_request = sdf_app_on_ble_admin_action_request,
  };
  err = sdf_ble_companion_init(&ble_companion_cbs);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Failed to initialize BLE companion service: %s",
             esp_err_to_name(err));
    if (degraded_err == ESP_OK) {
      degraded_err = err;
    }
  }

  if (sdf_protocol_zigbee_is_enabled()) {
    err = sdf_protocol_zigbee_set_checkin_interval_ms(cfg->checkin_interval_ms);
    if (err != ESP_OK) {
      ESP_LOGW(TAG, "Failed to set Zigbee check-in interval: %s",
               esp_err_to_name(err));
      if (degraded_err == ESP_OK) {
        degraded_err = err;
      }
    }

    err = sdf_protocol_zigbee_set_command_handler(sdf_app_on_zigbee_command,
                                                 NULL);
    if (err != ESP_OK) {
      ESP_LOGW(TAG, "Failed to set Zigbee command handler: %s",
               esp_err_to_name(err));
      if (degraded_err == ESP_OK) {
        degraded_err = err;
      }
    }

    err = sdf_protocol_zigbee_init();
    if (err != ESP_OK) {
      ESP_LOGW(TAG, "Failed to start Zigbee protocol: %s",
               esp_err_to_name(err));
      if (degraded_err == ESP_OK) {
        degraded_err = err;
      }
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
    if (degraded_err == ESP_OK) {
      degraded_err = err;
    }
  }

  err = sdf_cli_init();
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Failed to initialize CLI: %s", esp_err_to_name(err));
    if (degraded_err == ESP_OK) {
      degraded_err = err;
    }
  }

  sdf_power_mark_activity();

  return degraded_err;
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
