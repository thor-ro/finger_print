#include "sdf_drivers.h"

esp_err_t sdf_drivers_init(const sdf_drivers_config_t *config) {
  if (config == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  esp_err_t err = led_init(&config->led);
  if (err != ESP_OK) {
    return err;
  }

  err = sdf_drivers_battery_adc_init(config->battery_adc_pin);
  if (err != ESP_OK) {
    led_deinit();
    return err;
  }

  err = fp_init(&config->fingerprint);
  if (err != ESP_OK) {
    led_deinit();
    return err;
  }

  return ESP_OK;
}

void sdf_drivers_deinit(void) {
  fp_deinit();
  led_deinit();
}
