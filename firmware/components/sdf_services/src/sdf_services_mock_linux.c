/**
 * @file sdf_services_mock_linux.c
 * @brief Linux host mock implementations of GPIO and LED strip functions.
 */
#ifdef CONFIG_IDF_TARGET_LINUX

#include "sdf_mock_linux_services.h"

/* --------------- GPIO mock functions --------------- */

#undef gpio_config
esp_err_t gpio_config_mock(const gpio_config_t *config) {
  (void)config;
  return ESP_OK;
}

esp_err_t gpio_install_isr_service(int flags) {
  (void)flags;
  return ESP_OK;
}

esp_err_t gpio_isr_handler_add(int gpio, gpio_isr_t isr, void *args) {
  (void)gpio;
  (void)isr;
  (void)args;
  return ESP_OK;
}

int gpio_get_level(int gpio) {
  (void)gpio;
  return 1;
}

esp_err_t gpio_set_level(int gpio, int level) {
  (void)gpio;
  (void)level;
  return ESP_OK;
}

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
