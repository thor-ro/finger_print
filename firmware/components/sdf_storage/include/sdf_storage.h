#ifndef SDF_STORAGE_H
#define SDF_STORAGE_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
  bool require_encrypted_nvs;
  bool nvs_encryption_enabled;
  bool nvs_keys_partition_present;
  bool nvs_keys_accessible;
} sdf_storage_security_status_t;

esp_err_t sdf_storage_init(void);

bool sdf_storage_nvs_security_ok(void);
esp_err_t
sdf_storage_get_security_status(sdf_storage_security_status_t *status_out);

esp_err_t sdf_storage_nuki_save(uint32_t authorization_id,
                                const uint8_t shared_key[32]);
esp_err_t sdf_storage_nuki_load(uint32_t *authorization_id,
                                uint8_t shared_key[32]);
esp_err_t sdf_storage_nuki_clear(void);

typedef struct {
  uint16_t pairing_svc_start;
  uint16_t pairing_svc_end;
  uint16_t keyturner_svc_start;
  uint16_t keyturner_svc_end;
  uint16_t gdio_handle;
  uint16_t gdio_cccd;
  uint16_t usdio_handle;
  uint16_t usdio_cccd;
} sdf_nuki_ble_handles_t;

esp_err_t sdf_storage_nuki_handles_save(const sdf_nuki_ble_handles_t *handles);
esp_err_t sdf_storage_nuki_handles_load(sdf_nuki_ble_handles_t *handles);

esp_err_t sdf_storage_ble_target_save(uint8_t addr_type, const uint8_t addr[6]);
esp_err_t sdf_storage_ble_target_load(uint8_t *addr_type, uint8_t addr[6]);

#endif /* SDF_STORAGE_H */
