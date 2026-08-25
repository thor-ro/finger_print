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
#define SDF_STORAGE_WEB_USER_HASH_LEN 32     /* SHA256, as received at REGISTER */
#define SDF_STORAGE_WEB_USER_SALT_LEN 16
#define SDF_STORAGE_WEB_USER_STRETCHED_LEN 32  /* PBKDF2-HMAC-SHA256 output */

typedef struct {
    char username[SDF_STORAGE_WEB_USER_NAME_MAX];
    uint8_t salt[SDF_STORAGE_WEB_USER_SALT_LEN];
    uint8_t stretched_credential[SDF_STORAGE_WEB_USER_STRETCHED_LEN];
    uint8_t permission;  /* 1=standard, 2=elevated, 3=admin */
    bool valid;
} sdf_storage_web_user_t;

esp_err_t sdf_storage_web_user_save(uint8_t index, const sdf_storage_web_user_t *user);
esp_err_t sdf_storage_web_user_load(uint8_t index, sdf_storage_web_user_t *user);
esp_err_t sdf_storage_web_user_clear(uint8_t index);
esp_err_t sdf_storage_web_user_count(size_t *count);
esp_err_t sdf_storage_web_user_find_by_name(const char *username, sdf_storage_web_user_t *user, uint8_t *index_out);
esp_err_t sdf_storage_web_user_clear_all(void);

/* Device-local pseudo-salt HMAC key, used to derive an indistinguishable
 * LOGIN_INIT challenge for usernames with no stored account (see
 * sdf_services_web_auth). Generated once on first use via esp_fill_random
 * and persisted; load-or-generate so callers never have to special-case
 * "not yet created". Cleared together with web user accounts by
 * sdf_storage_erase_all so the two can never drift out of sync. */
#define SDF_STORAGE_WEB_PSEUDO_SALT_KEY_LEN 32

esp_err_t sdf_storage_web_pseudo_salt_key_load_or_generate(uint8_t key_out[SDF_STORAGE_WEB_PSEUDO_SALT_KEY_LEN]);

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

/* Enrolled-user cache: a bitmap (1 bit/user, IDs 1-10) plus packed
 * permissions (2 bits/user) persisted as the authoritative record of which
 * fingerprint users are enrolled and their permission level. Loaded
 * synchronously by sdf_services_init() so enrolled-user state is correct
 * from boot, without a live sensor query (see cache-enrolled-user-state).
 * Sized for 10 users (SDF_STORAGE_FP_USER_ID_MAX): ceil(10 * 2 bits / 8) = 3
 * bytes. Duplicated here rather than depending on sdf_services' own packed
 * size constant, for the same layering reason as SDF_STORAGE_FP_USER_ID_MAX
 * above - keep in sync if the user capacity ever changes. */
#define SDF_STORAGE_ENROLLED_USERS_PERM_PACKED_LEN 3u

esp_err_t sdf_storage_enrolled_users_save(uint16_t bmp, const uint8_t *perm_packed);
esp_err_t sdf_storage_enrolled_users_load(uint16_t *bmp_out, uint8_t *perm_packed_out);

/* Setup-completion latch. Written once at explicit setup completion, cleared
 * only by factory reset (sdf_storage_erase_all). An absent key reads as
 * "not complete" (false) with ESP_OK, per the component's absent-key
 * convention - a device upgrading to this firmware boots as unclaimed. */
esp_err_t sdf_storage_setup_complete_save(bool complete);
esp_err_t sdf_storage_setup_complete_load(bool *complete_out);
esp_err_t sdf_storage_setup_complete_clear(void);

/* Admission records: the identities that were deliberately granted
 * allow-list trust (setup completion or a pairing-window admit), stored as
 * addr type plus 6-byte address like sdf_storage_ble_target_save(). Sized to
 * hold at least CONFIG_BT_NIMBLE_MAX_BONDS (3, see sdkconfig.defaults) so
 * every persisted bond can have a matching record; the allow list is seeded
 * from the intersection of the two stores. */
#define SDF_STORAGE_ADMISSION_MAX 4u

typedef struct {
  uint8_t addr_type;
  uint8_t addr[6];
} sdf_storage_admission_t;

esp_err_t sdf_storage_admission_add(uint8_t addr_type, const uint8_t addr[6]);
esp_err_t sdf_storage_admission_remove(uint8_t addr_type, const uint8_t addr[6]);
/* Loads all admission records into up to max_entries slots of entries[];
 * count_out receives the number written. Returns ESP_ERR_NO_MEM if more
 * records exist than fit. An empty/absent store yields count 0 with ESP_OK. */
esp_err_t sdf_storage_admission_load_all(sdf_storage_admission_t *entries,
                                         size_t max_entries, size_t *count_out);
esp_err_t sdf_storage_admission_clear_all(void);

#endif /* SDF_STORAGE_H */
