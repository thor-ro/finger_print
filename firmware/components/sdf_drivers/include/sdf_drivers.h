#ifndef SDF_DRIVERS_H
#define SDF_DRIVERS_H

#include "battery.h"
#include "fingerprint.h"
#include "led.h"

typedef struct {
  sdf_fingerprint_driver_config_t fingerprint;
  sdf_led_config_t led;
  int battery_adc_pin;
} sdf_drivers_config_t;


esp_err_t sdf_drivers_init(const sdf_drivers_config_t *config);
void sdf_drivers_deinit(void);

#endif /* SDF_DRIVERS_H */
