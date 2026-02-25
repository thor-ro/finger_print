/**
 * @file sdf_protocol_zigbee_mock_linux.c
 * @brief Linux host mock implementations for the Zigbee protocol component.
 */
#ifdef CONFIG_IDF_TARGET_LINUX

#include "sdf_protocol_zigbee.h"
#include <stddef.h>
#include <stdint.h>

esp_err_t sdf_protocol_zigbee_init(void) { return ESP_OK; }

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

#endif /* CONFIG_IDF_TARGET_LINUX */
