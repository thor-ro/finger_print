#ifndef SDF_PLATFORM_GPIO_H
#define SDF_PLATFORM_GPIO_H

#include "sdkconfig.h"
#include <stdint.h>
#include <stdbool.h>

#ifndef CONFIG_IDF_TARGET_LINUX
#include "driver/gpio.h"
#else
#include "hal/gpio_types.h"
#include "sdf_mock_linux_gpio.h"
#endif

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief GPIO interrupt handler function type.
 */
typedef void (*sdf_platform_gpio_isr_t)(void *arg);

/**
 * @brief Configure a GPIO pin for wake/interrupt usage.
 *
 * @param gpio_num        GPIO number to configure.
 * @param intr_type       Interrupt type (GPIO_INTR_ANYEDGE, GPIO_INTR_POSEDGE, etc.).
 * @param pull_up         Enable internal pull-up.
 * @param pull_down       Enable internal pull-down.
 * @param isr_handler     ISR callback (can be NULL for wake-only pins).
 * @param isr_arg         Argument passed to ISR.
 * @return ESP_OK on success.
 */
esp_err_t sdf_platform_gpio_configure_wake(gpio_num_t gpio_num,
                                           gpio_int_type_t intr_type,
                                           bool pull_up,
                                           bool pull_down,
                                           sdf_platform_gpio_isr_t isr_handler,
                                           void *isr_arg);

/**
 * @brief Configure a GPIO pin as output.
 *
 * @param gpio_num    GPIO number.
 * @param initial_level Initial output level (0 or 1).
 * @return ESP_OK on success.
 */
esp_err_t sdf_platform_gpio_configure_output(gpio_num_t gpio_num, int initial_level);

/**
 * @brief Set GPIO output level.
 *
 * @param gpio_num GPIO number.
 * @param level    Output level (0 or 1).
 * @return ESP_OK on success.
 */
esp_err_t sdf_platform_gpio_set_level(gpio_num_t gpio_num, int level);

/**
 * @brief Get GPIO input level.
 *
 * @param gpio_num GPIO number.
 * @return Level (0 or 1), or -1 on error.
 */
int sdf_platform_gpio_get_level(gpio_num_t gpio_num);

/**
 * @brief Install GPIO ISR service (call once at startup).
 *
 * @param flags Flags for ISR service (usually 0).
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if already installed.
 */
esp_err_t sdf_platform_gpio_install_isr_service(int flags);

/**
 * @brief Uninstall GPIO ISR service.
 */
void sdf_platform_gpio_uninstall_isr_service(void);

/**
 * @brief Add ISR handler for a specific GPIO.
 *
 * @param gpio_num   GPIO number.
 * @param isr_handler ISR callback.
 * @param arg        Argument passed to ISR.
 * @return ESP_OK on success.
 */
esp_err_t sdf_platform_gpio_isr_handler_add(gpio_num_t gpio_num,
                                            gpio_isr_t isr_handler,
                                            void *arg);

/**
 * @brief Remove ISR handler for a specific GPIO.
 *
 * @param gpio_num GPIO number.
 * @return ESP_OK on success.
 */
esp_err_t sdf_platform_gpio_isr_handler_remove(gpio_num_t gpio_num);

/**
 * @brief Check if a GPIO is RTC-capable (can wake from deep sleep).
 *
 * On ESP32-C6, GPIOs 0-7 are RTC-capable.
 *
 * @param gpio_num GPIO number.
 * @return true if RTC-capable.
 */
bool sdf_platform_gpio_is_rtc_capable(gpio_num_t gpio_num);

#ifdef __cplusplus
}
#endif

#endif /* SDF_PLATFORM_GPIO_H */