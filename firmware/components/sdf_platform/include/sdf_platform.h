#ifndef SDF_PLATFORM_H
#define SDF_PLATFORM_H

#include "sdkconfig.h"
#include <stdint.h>
#include <stdbool.h>

#include "sdf_platform_gpio.h"
#include "sdf_platform_sleep.h"
#include "sdf_platform_time.h"
#include "sdf_platform_nvs.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize platform abstraction layer.
 *
 * Sets up GPIO ISR service, timer subsystem, and verifies NVS security.
 * Must be called before any other sdf_platform APIs.
 *
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t sdf_platform_init(void);

/**
 * @brief Deinitialize platform abstraction layer.
 *
 * Cleans up GPIO ISR service and other platform resources.
 */
void sdf_platform_deinit(void);

/**
 * @brief Check if platform is initialized.
 * @return true if initialized, false otherwise.
 */
bool sdf_platform_is_initialized(void);

#ifdef __cplusplus
}
#endif

#endif /* SDF_PLATFORM_H */