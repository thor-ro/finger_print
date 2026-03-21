#ifndef BATTERY_H
#define BATTERY_H

#include "esp_err.h"

esp_err_t sdf_drivers_battery_adc_init(int adc_pin);
int sdf_drivers_battery_get_percent(void);

#endif /* BATTERY_H */
