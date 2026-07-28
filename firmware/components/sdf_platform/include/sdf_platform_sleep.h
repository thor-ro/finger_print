#ifndef SDF_PLATFORM_SLEEP_H
#define SDF_PLATFORM_SLEEP_H

#include "sdkconfig.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifndef CONFIG_IDF_TARGET_LINUX
#include "esp_sleep.h"
#include "driver/gpio.h"
#else
#include "sdf_mock_linux_sleep.h"
#endif

#include "esp_err.h"

typedef enum {
    SDF_WAKE_SRC_TIMER = 0x01,
    SDF_WAKE_SRC_FINGERPRINT_GPIO = 0x02,
    SDF_WAKE_SRC_USB = 0x04,
    SDF_WAKE_SRC_BLE_ACTIVITY = 0x08,
    SDF_WAKE_SRC_ZIGBEE_RX = 0x10,
    SDF_WAKE_SRC_BUTTON = 0x20,
} sdf_wake_source_t;

typedef struct {
    uint32_t magic;
    uint8_t ble_transport_state;
    uint8_t zigbee_join_state;
    uint8_t sensor_power_state;
    uint8_t enrolled_user_count;
    uint32_t failed_attempts;
    int64_t last_activity_us;
    int64_t next_checkin_us;
    uint16_t crc16;
} sdf_power_retention_t;

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

/**
  * @brief Check if deep sleep wakeup was caused by timer.
  *
  * @return true if wakeup was from timer.
  */
bool sdf_platform_sleep_wakeup_from_timer(void);

/**
  * @brief Check if wakeup was caused by USB connection.
  *
  * @return true if wakeup was from USB.
  */
bool sdf_platform_sleep_wakeup_from_usb(void);

/**
  * @brief Configure wake sources for deep sleep.
  *
  * @param sources Bitmask of wake sources (see sdf_wake_source_t).
  * @return ESP_OK on success.
  */
esp_err_t sdf_platform_sleep_configure_wake_sources(uint32_t sources);

/**
  * @brief Write to RTC retention memory.
  *
  * @param data Data to write.
  * @param len Length of data (max CONFIG_SDF_POWER_RETENTION_SIZE).
  * @return ESP_OK on success.
  */
esp_err_t sdf_platform_sleep_retention_write(const void *data, size_t len);

/**
  * @brief Read from RTC retention memory.
  *
  * @param data Buffer to store read data.
  * @param len Length of data (max CONFIG_SDF_POWER_RETENTION_SIZE).
  * @return ESP_OK on success.
  */
esp_err_t sdf_platform_sleep_retention_read(void *data, size_t len);

/**
  * @brief Validate retention memory has correct magic.
  *
  * @return true if retention memory is valid.
  */
bool sdf_platform_sleep_retention_valid(void);

#ifdef __cplusplus
}
#endif

#endif /* SDF_PLATFORM_SLEEP_H */