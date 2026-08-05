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
esp_err_t sdf_storage_ble_target_clear(void);

esp_err_t sdf_storage_erase_all(void);

/* Web user account storage (max 5 users) */
#define SDF_STORAGE_WEB_USER_MAX 5
#define SDF_STORAGE_WEB_USER_NAME_MAX 32
#define SDF_STORAGE_WEB_USER_HASH_LEN 32  /* SHA256 */

typedef struct {
    char username[SDF_STORAGE_WEB_USER_NAME_MAX];
    uint8_t password_hash[SDF_STORAGE_WEB_USER_HASH_LEN];
    uint8_t permission;  /* 1=standard, 2=elevated, 3=admin */
    bool valid;
} sdf_storage_web_user_t;

esp_err_t sdf_storage_web_user_save(uint8_t index, const sdf_storage_web_user_t *user);
esp_err_t sdf_storage_web_user_load(uint8_t index, sdf_storage_web_user_t *user);
esp_err_t sdf_storage_web_user_clear(uint8_t index);
esp_err_t sdf_storage_web_user_count(size_t *count);
esp_err_t sdf_storage_web_user_find_by_name(const char *username, sdf_storage_web_user_t *user, uint8_t *index_out);
esp_err_t sdf_storage_web_user_clear_all(void);

/* Fingerprint user name storage (max 10 users, user_id 1-10). Duplicated
 * here (rather than depending on sdf_drivers/fingerprint.h's
 * SDF_FINGERPRINT_USER_ID_MIN/MAX) to avoid a storage->drivers layering
 * dependency - keep these two definitions in sync if the sensor's user
 * capacity ever changes. */
#define SDF_STORAGE_FP_USER_NAME_MAX 32
#define SDF_STORAGE_FP_USER_ID_MIN 1u
#define SDF_STORAGE_FP_USER_ID_MAX 10u

esp_err_t sdf_storage_save_user_name(uint16_t user_id, const char *name);
esp_err_t sdf_storage_load_user_name(uint16_t user_id, char *name_out, size_t max_len);
esp_err_t sdf_storage_delete_user_name(uint16_t user_id);

#endif /* SDF_STORAGE_H */
