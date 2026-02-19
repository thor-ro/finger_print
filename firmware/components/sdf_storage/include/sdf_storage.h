#ifndef SDF_STORAGE_H
#define SDF_STORAGE_H

#include <stdint.h>
#include <stdbool.h>

#include "esp_err.h"

typedef struct {
    bool require_encrypted_nvs;
    bool nvs_encryption_enabled;
    bool nvs_keys_partition_present;
    bool nvs_keys_accessible;
} sdf_storage_security_status_t;

esp_err_t sdf_storage_init(void);

bool sdf_storage_nvs_security_ok(void);
esp_err_t sdf_storage_get_security_status(sdf_storage_security_status_t *status_out);

esp_err_t sdf_storage_nuki_save(uint32_t authorization_id, const uint8_t shared_key[32]);
esp_err_t sdf_storage_nuki_load(uint32_t *authorization_id, uint8_t shared_key[32]);
esp_err_t sdf_storage_nuki_clear(void);

#endif /* SDF_STORAGE_H */
