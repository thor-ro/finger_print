#ifndef SDF_PLATFORM_NVS_H
#define SDF_PLATFORM_NVS_H

#include "sdkconfig.h"
#include <stdint.h>
#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief NVS security status.
 */
typedef struct {
    bool require_encrypted_nvs;     /**< Encrypted NVS required by config */
    bool nvs_encryption_enabled;    /**< NVS encryption actually enabled */
    bool nvs_keys_partition_present; /**< NVS keys partition exists */
    bool nvs_keys_accessible;       /**< NVS keys can be read */
} sdf_platform_nvs_security_status_t;

/**
 * @brief Initialize NVS with security verification.
 *
 * Initializes NVS flash and optionally verifies encryption requirements.
 *
 * @param require_encrypted If true, fail if NVS encryption is not properly configured.
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t sdf_platform_nvs_init(bool require_encrypted);

/**
 * @brief Get NVS security status.
 *
 * @param status Output structure for security status.
 * @return ESP_OK on success.
 */
esp_err_t sdf_platform_nvs_get_security_status(sdf_platform_nvs_security_status_t *status);

/**
 * @brief Check if NVS security policy is satisfied.
 *
 * Returns true if:
 * - Encryption is not required, OR
 * - Encryption is required AND all security checks pass.
 *
 * @return true if security OK, false otherwise.
 */
bool sdf_platform_nvs_security_ok(void);

/**
 * @brief Erase all NVS data.
 *
 * @return ESP_OK on success.
 */
esp_err_t sdf_platform_nvs_erase_all(void);

/**
 * @brief Deinitialize NVS.
 */
void sdf_platform_nvs_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* SDF_PLATFORM_NVS_H */