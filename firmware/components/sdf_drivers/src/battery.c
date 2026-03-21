#include "battery.h"

#include <stdbool.h>

#ifndef CONFIG_IDF_TARGET_LINUX
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "hal/adc_types.h"

static const char *TAG = "battery";

static adc_oneshot_unit_handle_t s_adc_handle = NULL;
static adc_cali_handle_t s_adc_cali_handle = NULL;
static adc_channel_t s_adc_channel = ADC_CHANNEL_0;
static bool s_adc_calibrated = false;

esp_err_t sdf_drivers_battery_adc_init(int adc_pin) {
  if (adc_pin < 0) {
    return ESP_ERR_INVALID_ARG;
  }

  adc_unit_t unit;
  esp_err_t err = adc_oneshot_io_to_channel(adc_pin, &unit, &s_adc_channel);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "ADC io to channel mapping failed for GPIO %d: %s", adc_pin,
             esp_err_to_name(err));
    return err;
  }

  adc_oneshot_unit_init_cfg_t init_config = {
      .unit_id = unit,
      .clk_src = ADC_DIGI_CLK_SRC_DEFAULT,
  };
  err = adc_oneshot_new_unit(&init_config, &s_adc_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "ADC new unit failed: %s", esp_err_to_name(err));
    return err;
  }

  adc_oneshot_chan_cfg_t config = {
      .bitwidth = ADC_BITWIDTH_DEFAULT,
      .atten = ADC_ATTEN_DB_12,
  };
  err = adc_oneshot_config_channel(s_adc_handle, s_adc_channel, &config);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "ADC config channel failed: %s", esp_err_to_name(err));
    return err;
  }

  adc_cali_curve_fitting_config_t cali_config = {
      .unit_id = unit,
      .chan = s_adc_channel,
      .atten = ADC_ATTEN_DB_12,
      .bitwidth = ADC_BITWIDTH_DEFAULT,
  };
  err = adc_cali_create_scheme_curve_fitting(&cali_config, &s_adc_cali_handle);
  if (err == ESP_OK) {
    s_adc_calibrated = true;
  } else {
    ESP_LOGW(TAG, "ADC calibration failed: %s", esp_err_to_name(err));
  }

  ESP_LOGI(TAG, "Battery ADC initialized on GPIO%d, unit %d, channel %d",
           adc_pin, unit, (int)s_adc_channel);
  return ESP_OK;
}

int sdf_drivers_battery_get_percent(void) {
  if (s_adc_handle == NULL) {
    return 100;
  }

  int raw = 0;
  esp_err_t err = adc_oneshot_read(s_adc_handle, s_adc_channel, &raw);
  if (err != ESP_OK) {
    return 100;
  }

  int voltage_mv = 0;
  if (s_adc_calibrated) {
    err = adc_cali_raw_to_voltage(s_adc_cali_handle, raw, &voltage_mv);
    if (err != ESP_OK) {
      voltage_mv = raw;
    }
  } else {
    voltage_mv = raw;
  }

  int battery_mv = voltage_mv * 2;

  if (battery_mv >= 3000) {
    return 100;
  }
  if (battery_mv <= 2000) {
    return 0;
  }

  return (battery_mv - 2000) * 100 / 1000;
}
#else
esp_err_t sdf_drivers_battery_adc_init(int adc_pin) {
  (void)adc_pin;
  return ESP_OK;
}

int sdf_drivers_battery_get_percent(void) { return 100; }
#endif
