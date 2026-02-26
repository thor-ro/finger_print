/**
 * @file sdf_protocol_zigbee_mock_linux.c
 * @brief Linux host mock implementations for the Zigbee protocol component.
 */
#include "sdkconfig.h"

#ifdef CONFIG_IDF_TARGET_LINUX

#include "sdf_protocol_zigbee.h"
#include <stddef.h>
#include <stdint.h>

esp_err_t sdf_protocol_zigbee_init(void) { return ESP_OK; }

esp_err_t
sdf_protocol_zigbee_set_command_handler(sdf_protocol_zigbee_command_cb cb,
                                        void *ctx) {
  (void)cb;
  (void)ctx;
  return ESP_OK;
}

esp_err_t sdf_protocol_zigbee_update_lock_state(
    sdf_protocol_zigbee_lock_state_t lock_state) {
  (void)lock_state;
  return ESP_OK;
}

esp_err_t sdf_protocol_zigbee_report_lock_state(uint8_t state) {
  (void)state;
  return ESP_OK;
}

esp_err_t sdf_protocol_zigbee_update_user_list(const char *json_array) {
  (void)json_array;
  return ESP_OK;
}

esp_err_t sdf_protocol_zigbee_set_checkin_interval_ms(uint32_t interval_ms) {
  (void)interval_ms;
  return ESP_OK;
}

uint32_t sdf_protocol_zigbee_get_checkin_interval_ms(void) { return 0; }

esp_err_t sdf_protocol_zigbee_update_alarm_mask(uint16_t alarm_mask) {
  (void)alarm_mask;
  return ESP_OK;
}

bool sdf_protocol_zigbee_is_ready(void) { return true; }

esp_err_t sdf_protocol_zigbee_update_battery_percent(uint8_t battery_percent) {
  (void)battery_percent;
  return ESP_OK;
}

#endif /* CONFIG_IDF_TARGET_LINUX */
