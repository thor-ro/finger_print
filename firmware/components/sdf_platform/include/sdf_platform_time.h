#ifndef SDF_PLATFORM_TIME_H
#define SDF_PLATFORM_TIME_H

#include "sdkconfig.h"
#include <stdint.h>
#include <stdbool.h>

#ifndef CONFIG_IDF_TARGET_LINUX
#include "esp_timer.h"
#else
#include <time.h>
#endif

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Get current time in microseconds since boot.
 *
 * Monotonic timer, safe for measuring intervals.
 *
 * @return Time in microseconds.
 */
int64_t sdf_platform_time_get_us(void);

/**
 * @brief Get current time in milliseconds since boot.
 *
 * @return Time in milliseconds.
 */
uint32_t sdf_platform_time_get_ms(void);

/**
 * @brief Delay for specified microseconds (blocking).
 *
 * @param us Microseconds to delay.
 */
void sdf_platform_time_delay_us(uint32_t us);

/**
 * @brief Delay for specified milliseconds (blocking).
 *
 * @param ms Milliseconds to delay.
 */
void sdf_platform_time_delay_ms(uint32_t ms);

/**
 * @brief Check if timeout has elapsed.
 *
 * @param start_us Start time from sdf_platform_time_get_us().
 * @param timeout_us Timeout in microseconds.
 * @return true if elapsed time >= timeout_us.
 */
bool sdf_platform_time_is_timeout(int64_t start_us, uint32_t timeout_us);

/**
 * @brief Calculate elapsed time since start.
 *
 * @param start_us Start time from sdf_platform_time_get_us().
 * @return Elapsed time in microseconds.
 */
int64_t sdf_platform_time_elapsed_us(int64_t start_us);

/**
 * @brief Reset task watchdog.
 *
 * Must be called periodically in long-running tasks.
 * No-op on Linux.
 */
void sdf_platform_time_wdt_reset(void);

/**
 * @brief Feed task watchdog with explicit timeout.
 *
 * @param timeout_ms Watchdog timeout in milliseconds.
 */
void sdf_platform_time_wdt_feed(uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* SDF_PLATFORM_TIME_H */