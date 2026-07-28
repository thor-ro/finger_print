#ifndef SDF_PLATFORM_POWER_H
#define SDF_PLATFORM_POWER_H

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

esp_err_t sdf_platform_power_init(void);
esp_err_t sdf_platform_power_enable_timer_wake(uint32_t interval_ms);
esp_err_t sdf_platform_power_enable_gpio_wake(int gpio_num, int level);
esp_err_t sdf_platform_power_disable_all_wake(void);
esp_err_t sdf_platform_power_enter_light(void);
esp_err_t sdf_platform_power_enter_deep(void);
esp_err_t sdf_platform_power_gate_ble_radio(bool enable);

#endif /* SDF_PLATFORM_POWER_H */