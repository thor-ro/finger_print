#include "sdf_platform_power.h"
#include "sdf_platform_sleep.h"

#include "esp_log.h"
#include "esp_sleep.h"

static const char *TAG = "sdf_platform_power";

esp_err_t sdf_platform_power_init(void) {
    return ESP_OK;
}

esp_err_t sdf_platform_power_enable_timer_wake(uint32_t interval_ms) {
    return sdf_platform_sleep_enable_timer_wakeup(interval_ms);
}

esp_err_t sdf_platform_power_enable_gpio_wake(int gpio_num, int level) {
    return sdf_platform_sleep_enable_gpio_wakeup_light((gpio_num_t)gpio_num, level);
}

esp_err_t sdf_platform_power_disable_all_wake(void) {
    return sdf_platform_sleep_disable_all_wakeup_sources();
}

esp_err_t sdf_platform_power_enter_light(void) {
    return sdf_platform_sleep_light();
}

esp_err_t sdf_platform_power_enter_deep(void) {
    return sdf_platform_sleep_deep();
}

esp_err_t sdf_platform_power_gate_ble_radio(bool enable) {
    (void)enable;
    return ESP_ERR_INVALID_STATE;
}