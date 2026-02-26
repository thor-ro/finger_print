/**
 * @file sdf_services_mock_linux.c
 * @brief Linux host mock implementations of LED strip functions.
 *
 * GPIO mocks are provided by sdf_mock_linux_gpio.c in sdf_common.
 */
#ifdef CONFIG_IDF_TARGET_LINUX

#include "sdf_mock_linux_services.h"

/* --------------- LED strip mock functions --------------- */

esp_err_t led_strip_new_rmt_device(const led_strip_config_t *led_config,
                                   const led_strip_rmt_config_t *rmt_config,
                                   led_strip_handle_t *ret_strip) {
  (void)led_config;
  (void)rmt_config;
  (void)ret_strip;
  return ESP_OK;
}

esp_err_t led_strip_set_pixel(led_strip_handle_t strip, uint32_t index,
                              uint32_t red, uint32_t green, uint32_t blue) {
  (void)strip;
  (void)index;
  (void)red;
  (void)green;
  (void)blue;
  return ESP_OK;
}

esp_err_t led_strip_refresh(led_strip_handle_t strip) {
  (void)strip;
  return ESP_OK;
}

esp_err_t led_strip_clear(led_strip_handle_t strip) {
  (void)strip;
  return ESP_OK;
}

#endif /* CONFIG_IDF_TARGET_LINUX */
