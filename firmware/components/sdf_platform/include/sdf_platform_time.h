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
 * @brief Register the calling task with the task watchdog.
 *
 * Must be called on task entry before entering the main loop.
 * On Linux, tracks registration for host test verification.
 */
void sdf_platform_time_wdt_add(void);

/**
 * @brief Deregister the calling task from the task watchdog.
 *
 * Must be called during cooperative task shutdown before task deletion.
 * On Linux, removes registration for host test verification.
 */
void sdf_platform_time_wdt_delete(void);

/**
 * @brief Reset task watchdog.
 *
 * Must be called periodically in long-running tasks.
 * On ESP32, resets the hardware task watchdog and logs a warning if the calling task is unregistered.
 * On Linux, verifies task registration and logs a one-shot warning if unregistered.
 */
void sdf_platform_time_wdt_reset(void);

/**
 * @brief Feed task watchdog with explicit timeout.
 *
 * @param timeout_ms Watchdog timeout in milliseconds.
 */
void sdf_platform_time_wdt_feed(uint32_t timeout_ms);

/**
 * @brief Check if a task is registered with the task watchdog.
 *
 * Used by host tests to verify task watchdog registration.
 * On hardware, always returns true.
 *
 * @param task_handle Task handle to check, or NULL for current task.
 * @return true if registered, false otherwise.
 */
bool sdf_platform_time_wdt_is_registered(void *task_handle);

/**
 * @brief Check if a task has triggered an unregistered watchdog reset warning.
 *
 * Used by host tests to verify that unregistered watchdog resets trigger
 * the one-shot diagnostic.
 * On hardware, always returns false.
 *
 * @param task_handle Task handle to check, or NULL for current task.
 * @return true if warned, false otherwise.
 */
bool sdf_platform_time_wdt_has_warned(void *task_handle);

/**
 * @brief Get total number of unregistered watchdog reset warnings emitted.
 *
 * Used by host tests to verify rate-limiting of the one-shot diagnostic.
 * On hardware, always returns 0.
 *
 * @return Total warning count.
 */
uint32_t sdf_platform_time_wdt_get_warning_count(void);

/**
 * @brief Clear warned task tracking state.
 *
 * Used by host tests between test cases.
 */
void sdf_platform_time_wdt_clear_warned_tasks(void);

#ifdef __cplusplus
}
#endif

#endif /* SDF_PLATFORM_TIME_H */