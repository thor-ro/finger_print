#include "sdf_protocol_zigbee.h"

#include <stdbool.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#ifndef CONFIG_IDF_TARGET_LINUX
#include "esp_check.h"
#include "esp_log.h"

#include "esp_zigbee_core.h"
#include "esp_zigbee_ota.h"
#include "ha/esp_zigbee_ha_standard.h"
#include "zcl/esp_zigbee_zcl_basic.h"
#include "zcl/esp_zigbee_zcl_command.h"
#include "zcl/esp_zigbee_zcl_diagnostics.h"
#include "zcl/esp_zigbee_zcl_door_lock.h"
#include "zcl/esp_zigbee_zcl_power_config.h"

#define SDF_ZIGBEE_ENDPOINT 1
#define SDF_ZIGBEE_TASK_NAME "sdf_zigbee"
#define SDF_ZIGBEE_TASK_STACK 6144
#define SDF_ZIGBEE_TASK_PRIORITY 5

#define SDF_ZIGBEE_INSTALL_CODE_POLICY false
#define SDF_ZIGBEE_ED_TIMEOUT ESP_ZB_ED_AGING_TIMEOUT_64MIN
#define SDF_ZIGBEE_CHECKIN_INTERVAL_DEFAULT_MS 3000U
#define SDF_ZIGBEE_CHECKIN_INTERVAL_MIN_MS 1000U
#define SDF_ZIGBEE_CHECKIN_INTERVAL_MAX_MS 600000U
#define SDF_ZIGBEE_PRIMARY_CHANNEL_MASK ESP_ZB_TRANSCEIVER_ALL_CHANNELS_MASK

/* Kconfig defaults (overridden by sdkconfig when available) */
#ifndef CONFIG_SDF_ZIGBEE_SLEEP_ENABLE
#define CONFIG_SDF_ZIGBEE_SLEEP_ENABLE 1
#endif
#ifndef CONFIG_SDF_ZIGBEE_SLEEP_THRESHOLD_MS
#define CONFIG_SDF_ZIGBEE_SLEEP_THRESHOLD_MS 20
#endif

static const char *TAG = "sdf_protocol_zigbee";

typedef struct {
  SemaphoreHandle_t lock;
  TaskHandle_t task;
  bool initialized;
  bool stack_started;
  bool network_joined;
  sdf_protocol_zigbee_command_cb command_cb;
  void *command_ctx;
  uint8_t lock_state;
  uint8_t battery_percent_remaining;
  uint16_t alarm_mask;
  uint32_t checkin_interval_ms;
} sdf_protocol_zigbee_state_t;

static sdf_protocol_zigbee_state_t s_state = {
    .lock = NULL,
    .task = NULL,
    .initialized = false,
    .stack_started = false,
    .network_joined = false,
    .command_cb = NULL,
    .command_ctx = NULL,
    .lock_state = SDF_PROTOCOL_ZIGBEE_LOCK_STATE_UNDEFINED,
    .battery_percent_remaining = 200,
    .alarm_mask = 0,
    .checkin_interval_ms = SDF_ZIGBEE_CHECKIN_INTERVAL_DEFAULT_MS,
};

static void sdf_zigbee_start_commissioning_cb(uint8_t mode_mask) {
  esp_err_t err = esp_zb_bdb_start_top_level_commissioning(mode_mask);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Commissioning start failed: %s", esp_err_to_name(err));
  }
}

static void sdf_zigbee_set_network_joined(bool joined) {
  if (s_state.lock == NULL) {
    return;
  }

  if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(250)) == pdTRUE) {
    s_state.network_joined = joined;
    xSemaphoreGive(s_state.lock);
  }
}

void esp_zb_app_signal_handler(esp_zb_app_signal_t *signal_struct) {
  if (signal_struct == NULL || signal_struct->p_app_signal == NULL) {
    return;
  }

  uint32_t *p_sg_p = signal_struct->p_app_signal;
  esp_zb_app_signal_type_t sig_type = *p_sg_p;
  esp_err_t err_status = signal_struct->esp_err_status;

  switch (sig_type) {
  case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP:
    ESP_LOGI(TAG, "Initialize Zigbee stack");
    esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_INITIALIZATION);
    break;

  case ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START:
  case ESP_ZB_BDB_SIGNAL_DEVICE_REBOOT:
    if (err_status == ESP_OK) {
      ESP_LOGI(TAG, "Device started in %s factory-reset mode",
               esp_zb_bdb_is_factory_new() ? "" : "non");
      if (esp_zb_bdb_is_factory_new()) {
        ESP_LOGI(TAG, "Start network steering");
        esp_zb_bdb_start_top_level_commissioning(
            ESP_ZB_BDB_MODE_NETWORK_STEERING);
      } else {
        sdf_zigbee_set_network_joined(true);
        ESP_LOGI(TAG, "Device rebooted and using existing network");
      }
    } else {
      ESP_LOGW(TAG, "Failed to initialize Zigbee stack (status=%s)",
               esp_err_to_name(err_status));
    }
    break;

  case ESP_ZB_BDB_SIGNAL_STEERING:
    if (err_status == ESP_OK) {
      esp_zb_ieee_addr_t extended_pan_id = {0};
      esp_zb_get_extended_pan_id(extended_pan_id);
      sdf_zigbee_set_network_joined(true);
      ESP_LOGI(TAG,
               "Joined network successfully (Extended PAN ID: "
               "%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x, PAN ID: 0x%04hx, "
               "Channel:%d, Short Address: 0x%04hx)",
               extended_pan_id[7], extended_pan_id[6], extended_pan_id[5],
               extended_pan_id[4], extended_pan_id[3], extended_pan_id[2],
               extended_pan_id[1], extended_pan_id[0], esp_zb_get_pan_id(),
               esp_zb_get_current_channel(), esp_zb_get_short_address());
    } else {
      sdf_zigbee_set_network_joined(false);
      ESP_LOGW(TAG, "Network steering failed (status=%s), scheduling retry",
               esp_err_to_name(err_status));
      esp_zb_scheduler_alarm(
          (esp_zb_callback_t)sdf_zigbee_start_commissioning_cb,
          ESP_ZB_BDB_MODE_NETWORK_STEERING, 1000);
    }
    break;

  default:
    ESP_LOGI(TAG, "ZDO signal: %s (0x%x), status: %s",
             esp_zb_zdo_signal_to_string(sig_type), sig_type,
             esp_err_to_name(err_status));
    break;

#if CONFIG_SDF_ZIGBEE_SLEEP_ENABLE
  case ESP_ZB_COMMON_SIGNAL_CAN_SLEEP:
    esp_zb_sleep_now();
    break;
#endif
  }
}

static bool sdf_zigbee_set_attr_u8(uint16_t cluster_id, uint16_t attr_id,
                                   uint8_t value) {
  if (!esp_zb_lock_acquire(pdMS_TO_TICKS(1000))) {
    ESP_LOGW(TAG,
             "Timeout acquiring Zigbee lock for cluster=0x%04X attr=0x%04X",
             cluster_id, attr_id);
    return false;
  }

  esp_zb_zcl_status_t status = esp_zb_zcl_set_attribute_val(
      SDF_ZIGBEE_ENDPOINT, cluster_id, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, attr_id,
      &value, false);
  esp_zb_lock_release();

  if (status != ESP_ZB_ZCL_STATUS_SUCCESS) {
    ESP_LOGW(TAG,
             "Failed to set Zigbee attribute cluster=0x%04X attr=0x%04X "
             "status=0x%02X",
             cluster_id, attr_id, (unsigned)status);
    return false;
  }

  return true;
}

static bool sdf_zigbee_set_attr_u16(uint16_t cluster_id, uint16_t attr_id,
                                    uint16_t value) {
  if (!esp_zb_lock_acquire(pdMS_TO_TICKS(1000))) {
    ESP_LOGW(TAG,
             "Timeout acquiring Zigbee lock for cluster=0x%04X attr=0x%04X",
             cluster_id, attr_id);
    return false;
  }

  esp_zb_zcl_status_t status = esp_zb_zcl_set_attribute_val(
      SDF_ZIGBEE_ENDPOINT, cluster_id, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, attr_id,
      &value, false);
  esp_zb_lock_release();

  if (status != ESP_ZB_ZCL_STATUS_SUCCESS) {
    ESP_LOGW(TAG,
             "Failed to set Zigbee attribute cluster=0x%04X attr=0x%04X "
             "status=0x%02X",
             cluster_id, attr_id, (unsigned)status);
    return false;
  }

  return true;
}

static bool sdf_zigbee_set_attr_string(uint16_t cluster_id, uint16_t attr_id,
                                       const char *value) {
  if (!esp_zb_lock_acquire(pdMS_TO_TICKS(1000))) {
    ESP_LOGW(TAG,
             "Timeout acquiring Zigbee lock for cluster=0x%04X attr=0x%04X",
             cluster_id, attr_id);
    return false;
  }

  size_t len = strlen(value);
  if (len > 254)
    len = 254; // Zigbee char string max length is 254

  // Zigbee Character String format: 1 byte length prefix + string data
  uint8_t zcl_str[255];
  zcl_str[0] = (uint8_t)len;
  memcpy(&zcl_str[1], value, len);

  esp_zb_zcl_status_t status = esp_zb_zcl_set_attribute_val(
      SDF_ZIGBEE_ENDPOINT, cluster_id, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, attr_id,
      zcl_str, false);
  esp_zb_lock_release();

  if (status != ESP_ZB_ZCL_STATUS_SUCCESS) {
    ESP_LOGW(TAG,
             "Failed to set Zigbee string attribute cluster=0x%04X attr=0x%04X "
             "status=0x%02X",
             cluster_id, attr_id, (unsigned)status);
    return false;
  }

  return true;
}

esp_err_t sdf_protocol_zigbee_update_user_list(const char *json_array) {
  if (json_array == NULL || s_state.lock == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(250)) != pdTRUE) {
    return ESP_ERR_TIMEOUT;
  }

  bool ok = sdf_zigbee_set_attr_string(ESP_ZB_ZCL_CLUSTER_ID_DOOR_LOCK,
                                       SDF_ZIGBEE_ATTR_ACTIVE_USERS_LIST_ID,
                                       json_array);

  xSemaphoreGive(s_state.lock);

  return ok ? ESP_OK : ESP_FAIL;
}

static void sdf_zigbee_apply_cached_attributes(void) {
  if (s_state.lock == NULL) {
    return;
  }

  uint8_t lock_state = SDF_PROTOCOL_ZIGBEE_LOCK_STATE_UNDEFINED;
  uint8_t battery_percent_remaining = 200;
  uint16_t alarm_mask = 0;

  if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(250)) != pdTRUE) {
    return;
  }
  lock_state = s_state.lock_state;
  battery_percent_remaining = s_state.battery_percent_remaining;
  alarm_mask = s_state.alarm_mask;
  xSemaphoreGive(s_state.lock);

  sdf_zigbee_set_attr_u8(ESP_ZB_ZCL_CLUSTER_ID_DOOR_LOCK,
                         ESP_ZB_ZCL_ATTR_DOOR_LOCK_LOCK_STATE_ID, lock_state);
  sdf_zigbee_set_attr_u8(
      ESP_ZB_ZCL_CLUSTER_ID_POWER_CONFIG,
      ESP_ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_PERCENTAGE_REMAINING_ID,
      battery_percent_remaining);
  sdf_zigbee_set_attr_u16(ESP_ZB_ZCL_CLUSTER_ID_DOOR_LOCK,
                          ESP_ZB_ZCL_ATTR_DOOR_LOCK_ALARM_MASK_ID, alarm_mask);
}

static esp_err_t sdf_zigbee_dispatch_command_event(
    const sdf_protocol_zigbee_command_event_t *event) {
  if (event == NULL || s_state.lock == NULL) {
    return ESP_ERR_INVALID_STATE;
  }

  sdf_protocol_zigbee_command_cb cb = NULL;
  void *ctx = NULL;

  if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(250)) == pdTRUE) {
    cb = s_state.command_cb;
    ctx = s_state.command_ctx;
    xSemaphoreGive(s_state.lock);
  }

  if (cb == NULL) {
    ESP_LOGW(TAG, "No command handler registered for Zigbee command %d",
             (int)event->command);
    return ESP_ERR_INVALID_STATE;
  }

  return cb(ctx, event);
}

static uint16_t sdf_zigbee_u16_from_le(const uint8_t *data) {
  return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static bool sdf_zigbee_is_programming_command(uint8_t cmd_id) {
  switch (cmd_id) {
  case ESP_ZB_ZCL_CMD_DOOR_LOCK_SET_PIN_CODE:
  case ESP_ZB_ZCL_CMD_DOOR_LOCK_CLEAR_PIN_CODE:
  case ESP_ZB_ZCL_CMD_DOOR_LOCK_CLEAR_ALL_PIN_CODES:
  case ESP_ZB_ZCL_CMD_DOOR_LOCK_SET_USER_STATUS:
  case ESP_ZB_ZCL_CMD_DOOR_LOCK_SET_USER_TYPE:
  case ESP_ZB_ZCL_CMD_DOOR_LOCK_SET_RFID_CODE:
  case ESP_ZB_ZCL_CMD_DOOR_LOCK_CLEAR_RFID_CODE:
  case ESP_ZB_ZCL_CMD_DOOR_LOCK_CLEAR_ALL_RFID_CODES:
    return true;
  default:
    return false;
  }
}

static esp_err_t sdf_zigbee_parse_programming_payload(
    const esp_zb_zcl_privilege_command_message_t *message,
    sdf_protocol_zigbee_programming_event_t *event) {
  if (message == NULL || event == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  memset(event, 0, sizeof(*event));
  event->zcl_command_id = message->info.command.id;
  event->src_endpoint = message->info.src_endpoint;
  if (message->info.src_address.addr_type == ESP_ZB_ZCL_ADDR_TYPE_SHORT) {
    event->src_short_addr = message->info.src_address.u.short_addr;
  }

  const uint8_t *payload = (const uint8_t *)message->data;
  size_t payload_len = message->size;

  switch (message->info.command.id) {
  case ESP_ZB_ZCL_CMD_DOOR_LOCK_SET_PIN_CODE:
  case ESP_ZB_ZCL_CMD_DOOR_LOCK_SET_RFID_CODE: {
    if (payload == NULL || payload_len < 5U) {
      return ESP_ERR_INVALID_SIZE;
    }

    event->has_user_id = true;
    event->user_id = sdf_zigbee_u16_from_le(payload);
    event->has_user_status = true;
    event->user_status = payload[2];
    event->has_user_type = true;
    event->user_type = payload[3];

    uint8_t credential_len = payload[4];
    if (payload_len < (size_t)(5U + credential_len)) {
      return ESP_ERR_INVALID_SIZE;
    }

    event->has_credential = true;
    event->credential_len = credential_len;
    if (event->credential_len > sizeof(event->credential)) {
      event->credential_len = sizeof(event->credential);
    }
    if (event->credential_len > 0U) {
      memcpy(event->credential, payload + 5, event->credential_len);
    }
    return ESP_OK;
  }

  case ESP_ZB_ZCL_CMD_DOOR_LOCK_CLEAR_PIN_CODE:
  case ESP_ZB_ZCL_CMD_DOOR_LOCK_CLEAR_RFID_CODE:
    if (payload == NULL || payload_len < 2U) {
      return ESP_ERR_INVALID_SIZE;
    }
    event->has_user_id = true;
    event->user_id = sdf_zigbee_u16_from_le(payload);
    return ESP_OK;

  case ESP_ZB_ZCL_CMD_DOOR_LOCK_SET_USER_STATUS:
    if (payload == NULL || payload_len < 3U) {
      return ESP_ERR_INVALID_SIZE;
    }
    event->has_user_id = true;
    event->user_id = sdf_zigbee_u16_from_le(payload);
    event->has_user_status = true;
    event->user_status = payload[2];
    return ESP_OK;

  case ESP_ZB_ZCL_CMD_DOOR_LOCK_SET_USER_TYPE:
    if (payload == NULL || payload_len < 3U) {
      return ESP_ERR_INVALID_SIZE;
    }
    event->has_user_id = true;
    event->user_id = sdf_zigbee_u16_from_le(payload);
    event->has_user_type = true;
    event->user_type = payload[2];
    return ESP_OK;

  case ESP_ZB_ZCL_CMD_DOOR_LOCK_CLEAR_ALL_PIN_CODES:
  case ESP_ZB_ZCL_CMD_DOOR_LOCK_CLEAR_ALL_RFID_CODES:
    return ESP_OK;

  default:
    return ESP_ERR_NOT_SUPPORTED;
  }
}

static esp_err_t sdf_zigbee_handle_door_lock_command(
    const esp_zb_zcl_door_lock_lock_door_message_t *message) {
  if (message == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  if (message->info.status != ESP_ZB_ZCL_STATUS_SUCCESS) {
    ESP_LOGW(TAG, "Received invalid Door Lock command status=0x%02X",
             (unsigned)message->info.status);
    return ESP_ERR_INVALID_ARG;
  }

  if (message->info.dst_endpoint != SDF_ZIGBEE_ENDPOINT) {
    return ESP_OK;
  }

  sdf_protocol_zigbee_command_event_t event = {
      .command = SDF_PROTOCOL_ZIGBEE_COMMAND_LOCK,
  };

  switch (message->cmd_id) {
  case ESP_ZB_ZCL_CMD_DOOR_LOCK_LOCK_DOOR:
    event.command = SDF_PROTOCOL_ZIGBEE_COMMAND_LOCK;
    break;
  case ESP_ZB_ZCL_CMD_DOOR_LOCK_UNLOCK_DOOR:
    event.command = SDF_PROTOCOL_ZIGBEE_COMMAND_UNLOCK;
    break;
  case ESP_ZB_ZCL_CMD_DOOR_LOCK_UNLOCK_WITH_TIMEOUT:
    /* Use Unlock With Timeout as "open door" semantic: unlock + unlatch
     * sequence. */
    event.command = SDF_PROTOCOL_ZIGBEE_COMMAND_LATCH;
    break;
  case ESP_ZB_ZCL_CMD_DOOR_LOCK_TOGGLE: {
    uint8_t current_lock_state = SDF_PROTOCOL_ZIGBEE_LOCK_STATE_UNDEFINED;
    if (s_state.lock != NULL &&
        xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(250)) == pdTRUE) {
      current_lock_state = s_state.lock_state;
      xSemaphoreGive(s_state.lock);
    }
    event.command =
        (current_lock_state == SDF_PROTOCOL_ZIGBEE_LOCK_STATE_LOCKED)
            ? SDF_PROTOCOL_ZIGBEE_COMMAND_UNLOCK
            : SDF_PROTOCOL_ZIGBEE_COMMAND_LOCK;
    break;
  }
  default:
    ESP_LOGW(TAG, "Unsupported Door Lock command id=0x%02X", message->cmd_id);
    return ESP_ERR_NOT_SUPPORTED;
  }

  ESP_LOGI(TAG, "Received Zigbee Door Lock command id=0x%02X -> action=%d",
           message->cmd_id, (int)event.command);
  esp_err_t err = sdf_zigbee_dispatch_command_event(&event);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Door Lock command dispatch failed: %s",
             esp_err_to_name(err));
  }
  return err;
}

static esp_err_t sdf_zigbee_handle_privilege_command(
    const esp_zb_zcl_privilege_command_message_t *message) {
  if (message == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  if (message->info.status != ESP_ZB_ZCL_STATUS_SUCCESS) {
    ESP_LOGW(TAG, "Received invalid privilege command status=0x%02X",
             (unsigned)message->info.status);
    return ESP_ERR_INVALID_ARG;
  }

  if (message->info.dst_endpoint != SDF_ZIGBEE_ENDPOINT ||
      message->info.cluster != ESP_ZB_ZCL_CLUSTER_ID_DOOR_LOCK ||
      message->info.command.direction != ESP_ZB_ZCL_CMD_DIRECTION_TO_SRV ||
      !sdf_zigbee_is_programming_command(message->info.command.id)) {
    return ESP_OK;
  }

  sdf_protocol_zigbee_command_event_t event = {
      .command = SDF_PROTOCOL_ZIGBEE_COMMAND_PROGRAMMING_EVENT,
  };

  esp_err_t parse_err =
      sdf_zigbee_parse_programming_payload(message, &event.programming_event);
  if (parse_err != ESP_OK) {
    ESP_LOGW(TAG, "Invalid programming command payload id=0x%02X size=%u: %s",
             message->info.command.id, (unsigned)message->size,
             esp_err_to_name(parse_err));
    return parse_err;
  }

  ESP_LOGI(TAG, "Programming command id=0x%02X from 0x%04X ep=%u",
           event.programming_event.zcl_command_id,
           event.programming_event.src_short_addr,
           (unsigned)event.programming_event.src_endpoint);

  esp_err_t err = sdf_zigbee_dispatch_command_event(&event);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Programming command dispatch failed: %s",
             esp_err_to_name(err));
  }
  return err;
}

static void sdf_zigbee_register_privilege_commands(void) {
  static const uint8_t programming_cmds[] = {
      ESP_ZB_ZCL_CMD_DOOR_LOCK_SET_PIN_CODE,
      ESP_ZB_ZCL_CMD_DOOR_LOCK_CLEAR_PIN_CODE,
      ESP_ZB_ZCL_CMD_DOOR_LOCK_CLEAR_ALL_PIN_CODES,
      ESP_ZB_ZCL_CMD_DOOR_LOCK_SET_USER_STATUS,
      ESP_ZB_ZCL_CMD_DOOR_LOCK_SET_USER_TYPE,
      ESP_ZB_ZCL_CMD_DOOR_LOCK_SET_RFID_CODE,
      ESP_ZB_ZCL_CMD_DOOR_LOCK_CLEAR_RFID_CODE,
      ESP_ZB_ZCL_CMD_DOOR_LOCK_CLEAR_ALL_RFID_CODES,
  };

  for (size_t i = 0;
       i < (sizeof(programming_cmds) / sizeof(programming_cmds[0])); ++i) {
    esp_err_t err = esp_zb_zcl_add_privilege_command(
        SDF_ZIGBEE_ENDPOINT, ESP_ZB_ZCL_CLUSTER_ID_DOOR_LOCK,
        programming_cmds[i]);
    if (err != ESP_OK) {
      ESP_LOGW(TAG, "Failed to register privilege command 0x%02X: %s",
               programming_cmds[i], esp_err_to_name(err));
    }
  }
}

static esp_err_t
sdf_zigbee_action_handler(esp_zb_core_action_callback_id_t callback_id,
                          const void *message) {
  switch (callback_id) {
  case ESP_ZB_CORE_DOOR_LOCK_LOCK_DOOR_CB_ID:
    return sdf_zigbee_handle_door_lock_command(
        (const esp_zb_zcl_door_lock_lock_door_message_t *)message);
  case ESP_ZB_CORE_CMD_PRIVILEGE_COMMAND_REQ_CB_ID:
    return sdf_zigbee_handle_privilege_command(
        (const esp_zb_zcl_privilege_command_message_t *)message);
  default:
    ESP_LOGD(TAG, "Unhandled Zigbee callback id=0x%04X", (unsigned)callback_id);
    return ESP_OK;
  }
}

static esp_err_t sdf_zigbee_register_endpoint(void) {
  esp_zb_door_lock_cfg_t door_lock_cfg = ESP_ZB_DEFAULT_DOOR_LOCK_CONFIG();
  door_lock_cfg.door_lock_cfg.lock_state = s_state.lock_state;

  esp_zb_cluster_list_t *cluster_list =
      esp_zb_door_lock_clusters_create(&door_lock_cfg);
  ESP_RETURN_ON_FALSE(cluster_list != NULL, ESP_ERR_NO_MEM, TAG,
                      "Failed to create Door Lock cluster list");

  esp_zb_attribute_list_t *door_lock_cluster = esp_zb_cluster_list_get_cluster(
      cluster_list, ESP_ZB_ZCL_CLUSTER_ID_DOOR_LOCK,
      ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
  ESP_RETURN_ON_FALSE(door_lock_cluster != NULL, ESP_FAIL, TAG,
                      "Door Lock cluster not found");

  esp_err_t err = esp_zb_door_lock_cluster_add_attr(
      door_lock_cluster, ESP_ZB_ZCL_ATTR_DOOR_LOCK_ALARM_MASK_ID,
      &s_state.alarm_mask);
  ESP_RETURN_ON_ERROR(err, TAG, "Failed to add Door Lock alarm mask attribute");

  esp_zb_power_config_cluster_cfg_t power_cfg = {
      .main_voltage = 0,
      .main_freq = 0,
      .main_alarm_mask = 0,
      .main_voltage_min = 0,
      .main_voltage_max = 0xFFFF,
      .main_voltage_dwell = 0,
  };

  esp_zb_attribute_list_t *power_cluster =
      esp_zb_power_config_cluster_create(&power_cfg);
  ESP_RETURN_ON_FALSE(power_cluster != NULL, ESP_ERR_NO_MEM, TAG,
                      "Failed to create Power Config cluster");

  err = esp_zb_power_config_cluster_add_attr(
      power_cluster,
      ESP_ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_PERCENTAGE_REMAINING_ID,
      &s_state.battery_percent_remaining);
  ESP_RETURN_ON_ERROR(err, TAG, "Failed to add battery percentage attribute");

  err = esp_zb_cluster_list_add_power_config_cluster(
      cluster_list, power_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
  ESP_RETURN_ON_ERROR(err, TAG, "Failed to add Power Config cluster");

  esp_zb_ota_cluster_cfg_t ota_cfg = {
      .ota_upgrade_file_version = 1,
      .ota_upgrade_manufacturer = 0x1011,
      .ota_upgrade_image_type = 0x1111,
      .ota_min_block_reque = 0,
      .ota_upgrade_file_offset = 0,
      .ota_upgrade_downloaded_file_ver = 0,
      .ota_image_upgrade_status = 0,
  };
  esp_zb_attribute_list_t *ota_cluster = esp_zb_ota_cluster_create(&ota_cfg);
  ESP_RETURN_ON_FALSE(ota_cluster != NULL, ESP_ERR_NO_MEM, TAG,
                      "Failed to create OTA cluster");
  err = esp_zb_cluster_list_add_ota_cluster(cluster_list, ota_cluster,
                                            ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE);
  ESP_RETURN_ON_ERROR(err, TAG, "Failed to add OTA cluster");

  esp_zb_diagnostics_cluster_cfg_t diag_cfg = {0};
  esp_zb_attribute_list_t *diag_cluster =
      esp_zb_diagnostics_cluster_create(&diag_cfg);
  ESP_RETURN_ON_FALSE(diag_cluster != NULL, ESP_ERR_NO_MEM, TAG,
                      "Failed to create Diagnostics cluster");
  err = esp_zb_cluster_list_add_diagnostics_cluster(
      cluster_list, diag_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
  ESP_RETURN_ON_ERROR(err, TAG, "Failed to add Diagnostics cluster");

  esp_zb_ep_list_t *ep_list = esp_zb_ep_list_create();
  ESP_RETURN_ON_FALSE(ep_list != NULL, ESP_ERR_NO_MEM, TAG,
                      "Failed to create endpoint list");

  esp_zb_endpoint_config_t endpoint_config = {
      .endpoint = SDF_ZIGBEE_ENDPOINT,
      .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
      .app_device_id = ESP_ZB_HA_DOOR_LOCK_DEVICE_ID,
      .app_device_version = 1,
  };

  err = esp_zb_ep_list_add_ep(ep_list, cluster_list, endpoint_config);
  ESP_RETURN_ON_ERROR(err, TAG, "Failed to add Door Lock endpoint");

  err = esp_zb_device_register(ep_list);
  ESP_RETURN_ON_ERROR(err, TAG, "Failed to register Zigbee endpoint");

  sdf_zigbee_register_privilege_commands();
  esp_zb_core_action_handler_register(sdf_zigbee_action_handler);
  return ESP_OK;
}

static void sdf_zigbee_task(void *arg) {
  (void)arg;

  uint32_t checkin_interval_ms = SDF_ZIGBEE_CHECKIN_INTERVAL_DEFAULT_MS;
  if (s_state.lock != NULL &&
      xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(250)) == pdTRUE) {
    checkin_interval_ms = s_state.checkin_interval_ms;
    xSemaphoreGive(s_state.lock);
  }

  esp_zb_cfg_t zb_nwk_cfg = {
      .esp_zb_role = ESP_ZB_DEVICE_TYPE_ED,
      .install_code_policy = SDF_ZIGBEE_INSTALL_CODE_POLICY,
      .nwk_cfg.zed_cfg =
          {
              .ed_timeout = SDF_ZIGBEE_ED_TIMEOUT,
              .keep_alive = checkin_interval_ms,
          },
  };

  esp_zb_init(&zb_nwk_cfg);
  esp_zb_set_primary_network_channel_set(SDF_ZIGBEE_PRIMARY_CHANNEL_MASK);

  if (sdf_zigbee_register_endpoint() != ESP_OK) {
    ESP_LOGE(TAG, "Failed to register Zigbee endpoint");
    goto fail;
  }

#if CONFIG_SDF_ZIGBEE_SLEEP_ENABLE
  esp_zb_sleep_enable(true);
  esp_zb_sleep_set_threshold(CONFIG_SDF_ZIGBEE_SLEEP_THRESHOLD_MS);
  ESP_LOGI(TAG, "Zigbee sleep enabled (threshold=%d ms)",
           CONFIG_SDF_ZIGBEE_SLEEP_THRESHOLD_MS);
#endif

  esp_err_t err = esp_zb_start(false);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start Zigbee stack: %s", esp_err_to_name(err));
    goto fail;
  }

  if (s_state.lock != NULL &&
      xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(250)) == pdTRUE) {
    s_state.stack_started = true;
    xSemaphoreGive(s_state.lock);
  }

  sdf_zigbee_apply_cached_attributes();
  ESP_LOGI(TAG, "Zigbee stack started (Door Lock endpoint %u)",
           (unsigned)SDF_ZIGBEE_ENDPOINT);
  esp_zb_stack_main_loop();

fail:
  if (s_state.lock != NULL &&
      xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(250)) == pdTRUE) {
    s_state.initialized = false;
    s_state.stack_started = false;
    s_state.network_joined = false;
    s_state.task = NULL;
    xSemaphoreGive(s_state.lock);
  }
  vTaskDelete(NULL);
}

esp_err_t sdf_protocol_zigbee_init(void) {
#if !defined(CONFIG_ZB_ENABLED) || (CONFIG_ZB_ENABLED == 0)
  ESP_LOGW(TAG, "Zigbee disabled in sdkconfig");
  return ESP_ERR_NOT_SUPPORTED;
#else
  if (s_state.lock == NULL) {
    s_state.lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_state.lock != NULL, ESP_ERR_NO_MEM, TAG,
                        "Failed to create Zigbee mutex");
  }

  if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(250)) != pdTRUE) {
    return ESP_ERR_TIMEOUT;
  }

  if (s_state.initialized) {
    xSemaphoreGive(s_state.lock);
    return ESP_OK;
  }
  s_state.initialized = true;
  xSemaphoreGive(s_state.lock);

  esp_zb_platform_config_t platform_config = {
      .radio_config =
          {
              .radio_mode = ZB_RADIO_MODE_NATIVE,
          },
      .host_config =
          {
              .host_connection_mode = ZB_HOST_CONNECTION_MODE_NONE,
          },
  };

  esp_err_t err = esp_zb_platform_config(&platform_config);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_zb_platform_config failed: %s", esp_err_to_name(err));
    if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(250)) == pdTRUE) {
      s_state.initialized = false;
      xSemaphoreGive(s_state.lock);
    }
    return err;
  }

  BaseType_t task_ok =
      xTaskCreate(sdf_zigbee_task, SDF_ZIGBEE_TASK_NAME, SDF_ZIGBEE_TASK_STACK,
                  NULL, SDF_ZIGBEE_TASK_PRIORITY, &s_state.task);

  if (task_ok != pdPASS) {
    ESP_LOGE(TAG, "Failed to create Zigbee task");
    if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(250)) == pdTRUE) {
      s_state.initialized = false;
      s_state.task = NULL;
      xSemaphoreGive(s_state.lock);
    }
    return ESP_FAIL;
  }

  return ESP_OK;
#endif
}

esp_err_t
sdf_protocol_zigbee_set_command_handler(sdf_protocol_zigbee_command_cb cb,
                                        void *ctx) {
  if (s_state.lock == NULL) {
    s_state.lock = xSemaphoreCreateMutex();
    if (s_state.lock == NULL) {
      return ESP_ERR_NO_MEM;
    }
  }

  if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(250)) != pdTRUE) {
    return ESP_ERR_TIMEOUT;
  }
  s_state.command_cb = cb;
  s_state.command_ctx = ctx;
  xSemaphoreGive(s_state.lock);
  return ESP_OK;
}

esp_err_t sdf_protocol_zigbee_update_lock_state(
    sdf_protocol_zigbee_lock_state_t lock_state) {
  if (lock_state != SDF_PROTOCOL_ZIGBEE_LOCK_STATE_NOT_FULLY_LOCKED &&
      lock_state != SDF_PROTOCOL_ZIGBEE_LOCK_STATE_LOCKED &&
      lock_state != SDF_PROTOCOL_ZIGBEE_LOCK_STATE_UNLOCKED &&
      lock_state != SDF_PROTOCOL_ZIGBEE_LOCK_STATE_UNDEFINED) {
    return ESP_ERR_INVALID_ARG;
  }

  if (s_state.lock == NULL) {
    return ESP_ERR_INVALID_STATE;
  }

  bool ready = false;
  if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(250)) == pdTRUE) {
    s_state.lock_state = (uint8_t)lock_state;
    ready = s_state.stack_started;
    xSemaphoreGive(s_state.lock);
  }

  if (!ready) {
    return ESP_OK;
  }

  return sdf_zigbee_set_attr_u8(ESP_ZB_ZCL_CLUSTER_ID_DOOR_LOCK,
                                ESP_ZB_ZCL_ATTR_DOOR_LOCK_LOCK_STATE_ID,
                                (uint8_t)lock_state)
             ? ESP_OK
             : ESP_FAIL;
}

esp_err_t sdf_protocol_zigbee_update_battery_percent(uint8_t battery_percent) {
  if (battery_percent > 100U) {
    return ESP_ERR_INVALID_ARG;
  }

  if (s_state.lock == NULL) {
    return ESP_ERR_INVALID_STATE;
  }

  uint8_t battery_percent_remaining = (uint8_t)(battery_percent * 2U);
  bool ready = false;
  if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(250)) == pdTRUE) {
    s_state.battery_percent_remaining = battery_percent_remaining;
    ready = s_state.stack_started;
    xSemaphoreGive(s_state.lock);
  }

  if (!ready) {
    return ESP_OK;
  }

  return sdf_zigbee_set_attr_u8(
             ESP_ZB_ZCL_CLUSTER_ID_POWER_CONFIG,
             ESP_ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_PERCENTAGE_REMAINING_ID,
             battery_percent_remaining)
             ? ESP_OK
             : ESP_FAIL;
}

esp_err_t sdf_protocol_zigbee_update_alarm_mask(uint16_t alarm_mask) {
  if (s_state.lock == NULL) {
    return ESP_ERR_INVALID_STATE;
  }

  bool ready = false;
  if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(250)) == pdTRUE) {
    s_state.alarm_mask = alarm_mask;
    ready = s_state.stack_started;
    xSemaphoreGive(s_state.lock);
  }

  if (!ready) {
    return ESP_OK;
  }

  return sdf_zigbee_set_attr_u16(ESP_ZB_ZCL_CLUSTER_ID_DOOR_LOCK,
                                 ESP_ZB_ZCL_ATTR_DOOR_LOCK_ALARM_MASK_ID,
                                 alarm_mask)
             ? ESP_OK
             : ESP_FAIL;
}

bool sdf_protocol_zigbee_is_ready(void) {
  if (s_state.lock == NULL) {
    return false;
  }

  bool ready = false;
  if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(250)) == pdTRUE) {
    ready = s_state.stack_started && s_state.network_joined;
    xSemaphoreGive(s_state.lock);
  }
  return ready;
}

esp_err_t sdf_protocol_zigbee_set_checkin_interval_ms(uint32_t interval_ms) {
  if (interval_ms < SDF_ZIGBEE_CHECKIN_INTERVAL_MIN_MS ||
      interval_ms > SDF_ZIGBEE_CHECKIN_INTERVAL_MAX_MS) {
    return ESP_ERR_INVALID_ARG;
  }

  if (s_state.lock == NULL) {
    s_state.checkin_interval_ms = interval_ms;
    return ESP_OK;
  }

  if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(250)) != pdTRUE) {
    return ESP_ERR_TIMEOUT;
  }

  if (s_state.stack_started) {
    xSemaphoreGive(s_state.lock);
    return ESP_ERR_INVALID_STATE;
  }

  s_state.checkin_interval_ms = interval_ms;
  xSemaphoreGive(s_state.lock);
  return ESP_OK;
}

esp_err_t sdf_protocol_zigbee_permit_join(void) {
  if (s_state.lock == NULL) {
    return ESP_ERR_INVALID_STATE;
  }

  bool ready = false;
  if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(250)) == pdTRUE) {
    ready = s_state.stack_started;
    xSemaphoreGive(s_state.lock);
  }

  if (!ready) {
    return ESP_ERR_INVALID_STATE;
  }

  esp_err_t err = esp_zb_bdb_start_top_level_commissioning(
      ESP_ZB_BDB_MODE_NETWORK_STEERING);
  if (err == ESP_OK) {
    ESP_LOGI(TAG, "Permit Join (Network Steering) enabled");
  } else {
    ESP_LOGW(TAG, "Failed to enable Permit Join: %s", esp_err_to_name(err));
  }

  return err;
}

uint32_t sdf_protocol_zigbee_get_checkin_interval_ms(void) {
  if (s_state.lock == NULL) {
    return s_state.checkin_interval_ms;
  }

  uint32_t interval_ms = s_state.checkin_interval_ms;
  if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(250)) == pdTRUE) {
    interval_ms = s_state.checkin_interval_ms;
    xSemaphoreGive(s_state.lock);
  }
  return interval_ms;
}
#endif /* !CONFIG_IDF_TARGET_LINUX */
