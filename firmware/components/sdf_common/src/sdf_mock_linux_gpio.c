/**
 * @file sdf_mock_linux_gpio.c
 * @brief Shared GPIO mock implementations for Linux host builds.
 */
#ifdef CONFIG_IDF_TARGET_LINUX

#include "sdf_mock_linux_gpio.h"

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

esp_err_t gpio_set_level(gpio_num_t gpio_num, uint32_t level) {
  (void)gpio_num;
  (void)level;
  return ESP_OK;
}

#endif /* CONFIG_IDF_TARGET_LINUX */
