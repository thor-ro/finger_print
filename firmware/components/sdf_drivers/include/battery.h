#ifndef BATTERY_H
#define BATTERY_H

#include "esp_err.h"

/*
 * Return contract for sdf_drivers_battery_get_percent():
 * 0..100 on a successful measurement; SDF_BATTERY_UNAVAILABLE (< 0) when
 * there is no reading — uninitialised sensor, failed read, or a build
 * without battery sensing. Never returns a substitute value.
 */
#define SDF_BATTERY_UNAVAILABLE (-1)

esp_err_t sdf_drivers_battery_adc_init(int adc_pin);
int sdf_drivers_battery_get_percent(void);

#endif /* BATTERY_H */
