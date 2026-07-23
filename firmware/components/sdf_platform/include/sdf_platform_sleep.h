#ifndef SDF_PLATFORM_SLEEP_H
#define SDF_PLATFORM_SLEEP_H

#include "sdkconfig.h"
#include <stdint.h>
#include <stdbool.h>

#ifndef CONFIG_IDF_TARGET_LINUX
#include "esp_sleep.h"
#include "driver/gpio.h"
#else
#include "sdf_mock_linux_sleep.h"
#endif

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Wakeup reason classification.
 */
typedef enum {
    SDF_PLATFORM_WAKE_REASON_NONE = 0,
    SDF_PLATFORM_WAKE_REASON_TIMER = 1,
    SDF_PLATFORM_WAKE_REASON_GPIO = 2,
    SDF_PLATFORM_WAKE_REASON_USB = 3,
    SDF_PLATFORM_WAKE_REASON_OTHER = 4,
} sdf_platform_wake_reason_t;

/**
 * @brief Map ESP sleep wakeup cause to platform wake reason.
 *
 * @param cause ESP sleep wakeup cause.
 * @return Platform wake reason.
 */
sdf_platform_wake_reason_t sdf_platform_map_wakeup_reason(esp_sleep_wakeup_cause_t cause);

/**
 * @brief Enable timer wakeup.
 *
 * @param interval_ms Timer interval in milliseconds.
 * @return ESP_OK on success.
 */
esp_err_t sdf_platform_sleep_enable_timer_wakeup(uint32_t interval_ms);

/**
 * @brief Enable GPIO wakeup (light sleep).
 *
 * @param gpio_num GPIO number.
 * @param level Wakeup level (0 = low, 1 = high).
 * @return ESP_OK on success.
 */
esp_err_t sdf_platform_sleep_enable_gpio_wakeup_light(gpio_num_t gpio_num, int level);

/**
 * @brief Enable GPIO wakeup (deep sleep).
 *
 * Only works on RTC-capable GPIOs (GPIO 0-7 on ESP32-C6).
 *
 * @param gpio_num GPIO number.
 * @param level Wakeup level (0 = low, 1 = high).
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if GPIO not RTC-capable.
 */
esp_err_t sdf_platform_sleep_enable_gpio_wakeup_deep(gpio_num_t gpio_num, int level);

/**
 * @brief Disable all wakeup sources.
 *
 * @return ESP_OK on success.
 */
esp_err_t sdf_platform_sleep_disable_all_wakeup_sources(void);

/**
 * @brief Enter light sleep.
 *
 * @return ESP_OK on wakeup, error code if sleep failed to start.
 */
esp_err_t sdf_platform_sleep_light(void);

/**
 * @brief Enter deep sleep.
 *
 * Does not return on success (device resets on wake).
 *
 * @return ESP_OK (never returns on success), error code on failure.
 */
esp_err_t sdf_platform_sleep_deep(void);

/**
 * @brief Get time spent in last sleep (microseconds).
 *
 * @return Sleep duration in microseconds, or 0 if not available.
 */
uint64_t sdf_platform_sleep_get_sleep_duration_us(void);

/**
 * @brief Check if deep sleep wakeup was caused by GPIO.
 *
 * @param gpio_num Pointer to store GPIO number (can be NULL).
 * @return true if wakeup was from GPIO.
 */
bool sdf_platform_sleep_wakeup_from_gpio(gpio_num_t *gpio_num);

#ifdef __cplusplus
}
#endif

#endif /* SDF_PLATFORM_SLEEP_H */