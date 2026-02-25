/**
 * @file sdf_protocol_ble_mock_linux.c
 * @brief Linux host mock implementations for the BLE protocol component.
 */
#ifdef CONFIG_IDF_TARGET_LINUX

#include "sdf_protocol_ble.h"
#include <stddef.h>
#include <stdint.h>

void sdf_protocol_ble_init(void) {}

esp_err_t sdf_protocol_ble_update_battery_percent(uint8_t percentage) {
  (void)percentage;
  return ESP_OK;
}

esp_err_t sdf_protocol_ble_update_lock_state(uint8_t state) {
  (void)state;
  return ESP_OK;
}

esp_err_t sdf_protocol_ble_update_doorsensor_state(uint8_t state) {
  (void)state;
  return ESP_OK;
}

esp_err_t sdf_protocol_ble_enable_beacon(bool enable) {
  (void)enable;
  return ESP_OK;
}

#endif /* CONFIG_IDF_TARGET_LINUX */
