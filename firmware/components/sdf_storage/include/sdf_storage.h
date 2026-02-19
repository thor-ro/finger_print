#ifndef SDF_STORAGE_H
#define SDF_STORAGE_H

#include <stdint.h>

#include "esp_err.h"

esp_err_t sdf_storage_init(void);

esp_err_t sdf_storage_nuki_save(uint32_t authorization_id, const uint8_t shared_key[32]);
esp_err_t sdf_storage_nuki_load(uint32_t *authorization_id, uint8_t shared_key[32]);
esp_err_t sdf_storage_nuki_clear(void);

#endif /* SDF_STORAGE_H */
