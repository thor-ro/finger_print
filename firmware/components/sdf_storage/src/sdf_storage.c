#include "sdf_storage.h"

#include <string.h>

#include "esp_log.h"
#include "esp_partition.h"
#include "esp_random.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#define SDF_STORAGE_NAMESPACE "nuki"
#define SDF_STORAGE_KEY_AUTH_ID "auth_id"
#define SDF_STORAGE_KEY_SHARED "shared_key"
#define SDF_STORAGE_KEY_BLE_HANDLES "ble_handles"
#define SDF_STORAGE_KEY_WEB_PSEUDO_SALT_KEY "web_pseudo_salt"
#define SDF_STORAGE_KEY_ENROLLED_USERS_BMP "enr_bmp"
#define SDF_STORAGE_KEY_ENROLLED_USERS_PERM "enr_perm"
#define SDF_STORAGE_KEY_SETUP_COMPLETE "setup_done"
#define SDF_STORAGE_KEY_ADMISSION_PREFIX "adm_"

static const char *TAG = "sdf_storage";
static sdf_storage_security_status_t s_security_status = {
    .require_encrypted_nvs = CONFIG_SDF_SECURITY_REQUIRE_ENCRYPTED_NVS,
#if CONFIG_NVS_ENCRYPTION
    .nvs_encryption_enabled = true,
#else
    .nvs_encryption_enabled = false,
#endif
    .nvs_keys_partition_present = false,
    .nvs_keys_accessible = false,
};

static esp_err_t sdf_storage_validate_security_policy(void) {
  const esp_partition_t *keys_partition = esp_partition_find_first(
      ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS_KEYS, NULL);

  s_security_status.nvs_keys_partition_present = (keys_partition != NULL);
  s_security_status.nvs_keys_accessible = false;

  if (!s_security_status.nvs_encryption_enabled) {
    if (s_security_status.require_encrypted_nvs) {
      ESP_LOGE(
          TAG,
          "Encrypted NVS is required, but CONFIG_NVS_ENCRYPTION is disabled");
      return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGW(TAG, "NVS encryption is disabled (build configuration)");
    return ESP_OK;
  }

  if (keys_partition == NULL) {
    ESP_LOGE(TAG,
             "NVS encryption enabled, but no nvs_keys partition was found");
    return s_security_status.require_encrypted_nvs ? ESP_ERR_NOT_FOUND : ESP_OK;
  }

  nvs_sec_cfg_t cfg = {0};
  esp_err_t read_err = nvs_flash_read_security_cfg(keys_partition, &cfg);
  if (read_err == ESP_OK) {
    s_security_status.nvs_keys_accessible = true;
    return ESP_OK;
  }

  if (read_err == ESP_ERR_NVS_KEYS_NOT_INITIALIZED ||
      read_err == ESP_ERR_NVS_CORRUPT_KEY_PART) {
    ESP_LOGI(TAG, "NVS key partition empty/corrupt, generating keys");
    esp_partition_erase_range(keys_partition, 0, keys_partition->size);
    read_err = nvs_flash_generate_keys(keys_partition, &cfg);
    if (read_err == ESP_OK) {
      s_security_status.nvs_keys_accessible = true;
      return ESP_OK;
    }
  }

  if (read_err == ESP_ERR_NVS_WRONG_ENCRYPTION) {
    ESP_LOGW(
        TAG,
        "NVS key partition present, but key config is not readable yet: %s",
        esp_err_to_name(read_err));
  } else {
    ESP_LOGW(TAG, "Unable to read NVS security config: %s",
             esp_err_to_name(read_err));
  }

  return s_security_status.require_encrypted_nvs ? read_err : ESP_OK;
}

esp_err_t sdf_storage_init(void) {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
      err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    esp_err_t erase_err = nvs_flash_erase();
    if (erase_err != ESP_OK) {
      return erase_err;
    }
    err = nvs_flash_init();
  }

  if (err != ESP_OK) {
    return err;
  }

  err = sdf_storage_validate_security_policy();
  if (err != ESP_OK) {
    return err;
  }

  if (s_security_status.nvs_encryption_enabled &&
      s_security_status.nvs_keys_partition_present) {
    ESP_LOGI(TAG, "Secure NVS policy verified");
  }
  return ESP_OK;
}

bool sdf_storage_nvs_security_ok(void) {
  if (!s_security_status.nvs_encryption_enabled &&
      s_security_status.require_encrypted_nvs) {
    return false;
  }

  if (!s_security_status.nvs_keys_partition_present &&
      s_security_status.require_encrypted_nvs) {
    return false;
  }

  if (s_security_status.require_encrypted_nvs &&
      !s_security_status.nvs_keys_accessible) {
    return false;
  }

  return true;
}

esp_err_t
sdf_storage_get_security_status(sdf_storage_security_status_t *status_out) {
  if (status_out == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  *status_out = s_security_status;
  return ESP_OK;
}

esp_err_t sdf_storage_nuki_save(uint32_t authorization_id,
                                const uint8_t shared_key[32]) {
  if (shared_key == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  nvs_handle_t handle;
  esp_err_t err = nvs_open(SDF_STORAGE_NAMESPACE, NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    return err;
  }

  err = nvs_set_u32(handle, SDF_STORAGE_KEY_AUTH_ID, authorization_id);
  if (err == ESP_OK) {
    err = nvs_set_blob(handle, SDF_STORAGE_KEY_SHARED, shared_key, 32);
  }
  if (err == ESP_OK) {
    err = nvs_commit(handle);
  }

  nvs_close(handle);
  return err;
}

esp_err_t sdf_storage_nuki_load(uint32_t *authorization_id,
                                uint8_t shared_key[32]) {
  if (authorization_id == NULL || shared_key == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  nvs_handle_t handle;
  esp_err_t err = nvs_open(SDF_STORAGE_NAMESPACE, NVS_READONLY, &handle);
  if (err != ESP_OK) {
    return err;
  }

  err = nvs_get_u32(handle, SDF_STORAGE_KEY_AUTH_ID, authorization_id);
  if (err == ESP_OK) {
    size_t len = 32;
    err = nvs_get_blob(handle, SDF_STORAGE_KEY_SHARED, shared_key, &len);
    if (err == ESP_OK && len != 32) {
      err = ESP_ERR_NVS_INVALID_LENGTH;
    }
  }

  nvs_close(handle);
  return err;
}

esp_err_t sdf_storage_nuki_clear(void) {
  nvs_handle_t handle;
  esp_err_t err = nvs_open(SDF_STORAGE_NAMESPACE, NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    return err;
  }

  err = nvs_erase_key(handle, SDF_STORAGE_KEY_AUTH_ID);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    err = ESP_OK;
  }

  if (err == ESP_OK) {
    esp_err_t key_err = nvs_erase_key(handle, SDF_STORAGE_KEY_SHARED);
    if (key_err == ESP_ERR_NVS_NOT_FOUND) {
      key_err = ESP_OK;
    }
    if (key_err != ESP_OK) {
      err = key_err;
    }
  }

  if (err == ESP_OK) {
    esp_err_t hnd_err = nvs_erase_key(handle, SDF_STORAGE_KEY_BLE_HANDLES);
    if (hnd_err == ESP_ERR_NVS_NOT_FOUND) {
      hnd_err = ESP_OK;
    }
    if (hnd_err != ESP_OK) {
      err = hnd_err;
    }
  }

  if (err == ESP_OK) {
    err = nvs_commit(handle);
  }

  nvs_close(handle);
  return err;
}

esp_err_t sdf_storage_nuki_handles_save(const sdf_nuki_ble_handles_t *handles) {
  if (handles == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  nvs_handle_t handle;
  esp_err_t err = nvs_open(SDF_STORAGE_NAMESPACE, NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    return err;
  }

  err = nvs_set_blob(handle, SDF_STORAGE_KEY_BLE_HANDLES, handles,
                     sizeof(sdf_nuki_ble_handles_t));
  if (err == ESP_OK) {
    err = nvs_commit(handle);
  }

  nvs_close(handle);
  return err;
}

esp_err_t sdf_storage_nuki_handles_load(sdf_nuki_ble_handles_t *handles) {
  if (handles == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  nvs_handle_t handle;
  esp_err_t err = nvs_open(SDF_STORAGE_NAMESPACE, NVS_READONLY, &handle);
  if (err != ESP_OK) {
    return err;
  }

  size_t len = sizeof(sdf_nuki_ble_handles_t);
  err = nvs_get_blob(handle, SDF_STORAGE_KEY_BLE_HANDLES, handles, &len);
  if (err == ESP_OK && len != sizeof(sdf_nuki_ble_handles_t)) {
    err = ESP_ERR_NVS_INVALID_LENGTH;
  }

  nvs_close(handle);
  return err;
}

#define SDF_STORAGE_KEY_BLE_TARGET "ble_target"
#define SDF_STORAGE_BLE_TARGET_LEN 7u

esp_err_t sdf_storage_ble_target_save(uint8_t addr_type,
                                      const uint8_t addr[6]) {
  if (addr == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  uint8_t blob[SDF_STORAGE_BLE_TARGET_LEN];
  blob[0] = addr_type;
  memcpy(&blob[1], addr, 6);

  nvs_handle_t handle;
  esp_err_t err = nvs_open(SDF_STORAGE_NAMESPACE, NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    return err;
  }

  err = nvs_set_blob(handle, SDF_STORAGE_KEY_BLE_TARGET, blob, sizeof(blob));
  if (err == ESP_OK) {
    err = nvs_commit(handle);
  }

  nvs_close(handle);
  return err;
}

esp_err_t sdf_storage_ble_target_load(uint8_t *addr_type, uint8_t addr[6]) {
  if (addr_type == NULL || addr == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  nvs_handle_t handle;
  esp_err_t err = nvs_open(SDF_STORAGE_NAMESPACE, NVS_READONLY, &handle);
  if (err != ESP_OK) {
    return err;
  }

  uint8_t blob[SDF_STORAGE_BLE_TARGET_LEN];
  size_t len = sizeof(blob);
  err = nvs_get_blob(handle, SDF_STORAGE_KEY_BLE_TARGET, blob, &len);
  if (err == ESP_OK && len == SDF_STORAGE_BLE_TARGET_LEN) {
    *addr_type = blob[0];
    memcpy(addr, &blob[1], 6);
  } else if (err == ESP_OK) {
    err = ESP_ERR_NVS_INVALID_LENGTH;
  }

  nvs_close(handle);
  return err;
}

esp_err_t sdf_storage_ble_target_clear(void) {
  nvs_handle_t handle;
  esp_err_t err = nvs_open(SDF_STORAGE_NAMESPACE, NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    return err;
  }

  err = nvs_erase_key(handle, SDF_STORAGE_KEY_BLE_TARGET);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    err = ESP_OK;
  }

  if (err == ESP_OK) {
    err = nvs_commit(handle);
  }

  nvs_close(handle);
  return err;
}

#define SDF_STORAGE_KEY_WEB_USER_PREFIX "web_user_"

static void sdf_storage_web_user_key(char *buf, size_t buf_size, uint16_t user_id) {
    snprintf(buf, buf_size, "%s%u", SDF_STORAGE_KEY_WEB_USER_PREFIX, (unsigned)user_id);
}

static bool sdf_storage_web_user_id_valid(uint16_t user_id) {
    return user_id >= SDF_STORAGE_FP_USER_ID_MIN && user_id <= SDF_STORAGE_FP_USER_ID_MAX;
}

esp_err_t sdf_storage_web_user_save(uint16_t user_id, const sdf_storage_web_user_t *user) {
    if (!sdf_storage_web_user_id_valid(user_id) || user == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char key[32];
    sdf_storage_web_user_key(key, sizeof(key), user_id);

    nvs_handle_t handle;
    esp_err_t err = nvs_open(SDF_STORAGE_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_blob(handle, key, user, sizeof(sdf_storage_web_user_t));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }

    nvs_close(handle);
    return err;
}

esp_err_t sdf_storage_web_user_load(uint16_t user_id, sdf_storage_web_user_t *user) {
    if (!sdf_storage_web_user_id_valid(user_id) || user == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char key[32];
    sdf_storage_web_user_key(key, sizeof(key), user_id);

    nvs_handle_t handle;
    esp_err_t err = nvs_open(SDF_STORAGE_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return err;
    }

    size_t len = sizeof(sdf_storage_web_user_t);
    err = nvs_get_blob(handle, key, user, &len);
    if (err == ESP_OK && len != sizeof(sdf_storage_web_user_t)) {
        err = ESP_ERR_NVS_INVALID_LENGTH;
    }

    nvs_close(handle);
    return err;
}

esp_err_t sdf_storage_web_user_clear(uint16_t user_id) {
    if (!sdf_storage_web_user_id_valid(user_id)) {
        return ESP_ERR_INVALID_ARG;
    }

    char key[32];
    sdf_storage_web_user_key(key, sizeof(key), user_id);

    nvs_handle_t handle;
    esp_err_t err = nvs_open(SDF_STORAGE_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_erase_key(handle, key);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = ESP_OK;
    }

    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }

    nvs_close(handle);
    return err;
}

esp_err_t sdf_storage_web_user_count(size_t *count) {
    if (count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(SDF_STORAGE_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return err;
    }

    size_t cnt = 0;
    for (uint16_t id = SDF_STORAGE_FP_USER_ID_MIN; id <= SDF_STORAGE_WEB_USER_MAX; id++) {
        char key[32];
        sdf_storage_web_user_key(key, sizeof(key), id);
        sdf_storage_web_user_t u;
        size_t len = sizeof(sdf_storage_web_user_t);
        err = nvs_get_blob(handle, key, &u, &len);
        /* Only records actually holding a companion credential are counted:
         * a name-only record is a person, not an account. */
        if (err == ESP_OK && u.valid && u.has_credential) {
            cnt++;
        } else if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
            /* ESP_ERR_NVS_NOT_FOUND just means this id has never been
             * written - expected for unenrolled users. Anything else (e.g. a
             * corrupted/truncated blob) is swallowed the same way here
             * (the record is simply not counted), but is worth logging since
             * it could otherwise silently undercount valid accounts. */
            ESP_LOGW(TAG, "web_user_count: unexpected error reading id %u: %s",
                     (unsigned)id, esp_err_to_name(err));
        }
    }

    nvs_close(handle);
    *count = cnt;
    return ESP_OK;
}

esp_err_t sdf_storage_web_user_find_by_name(const char *name, sdf_storage_web_user_t *user, uint16_t *id_out) {
    if (name == NULL || user == NULL || id_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(SDF_STORAGE_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return err;
    }

    for (uint16_t id = SDF_STORAGE_FP_USER_ID_MIN; id <= SDF_STORAGE_WEB_USER_MAX; id++) {
        char key[32];
        sdf_storage_web_user_key(key, sizeof(key), id);
        sdf_storage_web_user_t u;
        size_t len = sizeof(sdf_storage_web_user_t);
        err = nvs_get_blob(handle, key, &u, &len);
        /* A record with a name but no credential is not an account: the name
         * is the login identifier only for users holding one, so such a
         * record reports as no match (LOGIN_INIT then treats the name as
         * unknown - see companion-identity design.md "Non-admin names answer
         * LOGIN_INIT as unknown"). */
        if (err == ESP_OK && u.valid && u.has_credential &&
            strcmp(u.name, name) == 0) {
            *user = u;
            *id_out = id;
            nvs_close(handle);
            return ESP_OK;
        }
    }

    nvs_close(handle);
    return ESP_ERR_NOT_FOUND;
}

esp_err_t sdf_storage_web_user_clear_all(void) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(SDF_STORAGE_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    for (uint16_t id = SDF_STORAGE_FP_USER_ID_MIN; id <= SDF_STORAGE_WEB_USER_MAX; id++) {
        char key[32];
        sdf_storage_web_user_key(key, sizeof(key), id);
        nvs_erase_key(handle, key);
    }

    err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

esp_err_t sdf_storage_web_pseudo_salt_key_load_or_generate(uint8_t key_out[SDF_STORAGE_WEB_PSEUDO_SALT_KEY_LEN]) {
    if (key_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(SDF_STORAGE_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    size_t len = SDF_STORAGE_WEB_PSEUDO_SALT_KEY_LEN;
    err = nvs_get_blob(handle, SDF_STORAGE_KEY_WEB_PSEUDO_SALT_KEY, key_out, &len);
    if (err == ESP_OK && len == SDF_STORAGE_WEB_PSEUDO_SALT_KEY_LEN) {
        nvs_close(handle);
        return ESP_OK;
    }

    /* Not found, or a corrupted/wrong-length blob - (re)generate. */
    esp_fill_random(key_out, SDF_STORAGE_WEB_PSEUDO_SALT_KEY_LEN);
    err = nvs_set_blob(handle, SDF_STORAGE_KEY_WEB_PSEUDO_SALT_KEY, key_out,
                        SDF_STORAGE_WEB_PSEUDO_SALT_KEY_LEN);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }

    nvs_close(handle);
    return err;
}

#ifdef SDF_STORAGE_TESTING
/* Test-only fault injection: when non-zero, the next N calls to
 * sdf_storage_enrolled_users_save() fail with ESP_FAIL instead of touching
 * NVS, decrementing this counter once per call. Lets host tests exercise
 * sdf_services_persist_enrolled_users_locked()'s retry-then-give-up
 * behavior deterministically, without a real NVS-level failure mode to
 * trigger (see cache-enrolled-user-state design.md). */
static uint32_t s_test_enrolled_users_save_fail_count = 0;

void test_sdf_storage_set_enrolled_users_save_fail_count(uint32_t count) {
  s_test_enrolled_users_save_fail_count = count;
}
#endif

esp_err_t sdf_storage_enrolled_users_save(uint16_t bmp, const uint8_t *perm_packed) {
  if (perm_packed == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

#ifdef SDF_STORAGE_TESTING
  if (s_test_enrolled_users_save_fail_count > 0) {
    s_test_enrolled_users_save_fail_count--;
    return ESP_FAIL;
  }
#endif

  nvs_handle_t handle;
  esp_err_t err = nvs_open(SDF_STORAGE_NAMESPACE, NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    return err;
  }

  err = nvs_set_u16(handle, SDF_STORAGE_KEY_ENROLLED_USERS_BMP, bmp);
  if (err == ESP_OK) {
    err = nvs_set_blob(handle, SDF_STORAGE_KEY_ENROLLED_USERS_PERM, perm_packed,
                       SDF_STORAGE_ENROLLED_USERS_PERM_PACKED_LEN);
  }
  if (err == ESP_OK) {
    err = nvs_commit(handle);
  }

  nvs_close(handle);
  return err;
}

esp_err_t sdf_storage_enrolled_users_load(uint16_t *bmp_out, uint8_t *perm_packed_out) {
  if (bmp_out == NULL || perm_packed_out == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  nvs_handle_t handle;
  esp_err_t err = nvs_open(SDF_STORAGE_NAMESPACE, NVS_READONLY, &handle);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    /* Namespace not created yet (first boot after this firmware update, or
     * an erased device) - treat as "zero users enrolled", not an error. */
    *bmp_out = 0;
    memset(perm_packed_out, 0, SDF_STORAGE_ENROLLED_USERS_PERM_PACKED_LEN);
    return ESP_OK;
  }
  if (err != ESP_OK) {
    return err;
  }

  uint16_t bmp = 0;
  err = nvs_get_u16(handle, SDF_STORAGE_KEY_ENROLLED_USERS_BMP, &bmp);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    *bmp_out = 0;
    memset(perm_packed_out, 0, SDF_STORAGE_ENROLLED_USERS_PERM_PACKED_LEN);
    nvs_close(handle);
    return ESP_OK;
  }
  if (err != ESP_OK) {
    nvs_close(handle);
    return err;
  }

  size_t len = SDF_STORAGE_ENROLLED_USERS_PERM_PACKED_LEN;
  err = nvs_get_blob(handle, SDF_STORAGE_KEY_ENROLLED_USERS_PERM, perm_packed_out, &len);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    *bmp_out = 0;
    memset(perm_packed_out, 0, SDF_STORAGE_ENROLLED_USERS_PERM_PACKED_LEN);
    nvs_close(handle);
    return ESP_OK;
  }
  if (err == ESP_OK && len != SDF_STORAGE_ENROLLED_USERS_PERM_PACKED_LEN) {
    err = ESP_ERR_NVS_INVALID_LENGTH;
  }
  if (err == ESP_OK) {
    *bmp_out = bmp;
  }

  nvs_close(handle);
  return err;
}

esp_err_t sdf_storage_setup_complete_save(bool complete) {
  nvs_handle_t handle;
  esp_err_t err = nvs_open(SDF_STORAGE_NAMESPACE, NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    return err;
  }

  err = nvs_set_u8(handle, SDF_STORAGE_KEY_SETUP_COMPLETE, complete ? 1u : 0u);
  if (err == ESP_OK) {
    err = nvs_commit(handle);
  }

  nvs_close(handle);
  return err;
}

esp_err_t sdf_storage_setup_complete_load(bool *complete_out) {
  if (complete_out == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  nvs_handle_t handle;
  esp_err_t err = nvs_open(SDF_STORAGE_NAMESPACE, NVS_READONLY, &handle);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    /* Namespace never created (first boot / erased device): latch unset. */
    *complete_out = false;
    return ESP_OK;
  }
  if (err != ESP_OK) {
    return err;
  }

  uint8_t value = 0;
  err = nvs_get_u8(handle, SDF_STORAGE_KEY_SETUP_COMPLETE, &value);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    /* Key never written: latch unset, not an error. */
    *complete_out = false;
    nvs_close(handle);
    return ESP_OK;
  }
  if (err == ESP_OK) {
    *complete_out = (value != 0);
  }

  nvs_close(handle);
  return err;
}

esp_err_t sdf_storage_setup_complete_clear(void) {
  nvs_handle_t handle;
  esp_err_t err = nvs_open(SDF_STORAGE_NAMESPACE, NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    return err;
  }

  err = nvs_erase_key(handle, SDF_STORAGE_KEY_SETUP_COMPLETE);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    err = ESP_OK;
  }

  if (err == ESP_OK) {
    err = nvs_commit(handle);
  }

  nvs_close(handle);
  return err;
}

static void sdf_storage_admission_key(char *buf, size_t buf_size,
                                      size_t index) {
  snprintf(buf, buf_size, "%s%u", SDF_STORAGE_KEY_ADMISSION_PREFIX,
           (unsigned)index);
}

esp_err_t sdf_storage_admission_add(uint8_t addr_type, const uint8_t addr[6]) {
  if (addr == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  uint8_t blob[7];
  blob[0] = addr_type;
  memcpy(&blob[1], addr, 6);

  nvs_handle_t handle;
  esp_err_t err = nvs_open(SDF_STORAGE_NAMESPACE, NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    return err;
  }

  /* Reuse an existing identical record before appending a new one. */
  bool already_present = false;
  bool any_error = false;
  size_t used = 0;
  for (size_t i = 0; i < SDF_STORAGE_ADMISSION_MAX; i++) {
    char key[32];
    sdf_storage_admission_key(key, sizeof(key), i);

    uint8_t stored[sizeof(blob)];
    size_t len = sizeof(stored);
    err = nvs_get_blob(handle, key, stored, &len);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
      continue; /* Empty slot - not an error. */
    }
    if (err != ESP_OK) {
      any_error = true;
      break;
    }
    used++;
    if (len == sizeof(blob) && memcmp(stored, blob, sizeof(blob)) == 0) {
      already_present = true;
    }
  }

  if (!any_error && !already_present) {
    if (used >= SDF_STORAGE_ADMISSION_MAX) {
      nvs_close(handle);
      return ESP_ERR_NO_MEM;
    }
    char key[32];
    sdf_storage_admission_key(key, sizeof(key), used);
    err = nvs_set_blob(handle, key, blob, sizeof(blob));
    if (err == ESP_OK) {
      err = nvs_commit(handle);
    }
  } else if (!any_error) {
    err = ESP_OK; /* Duplicate admitted once; nothing to write. */
  }

  nvs_close(handle);
  return err;
}

esp_err_t sdf_storage_admission_remove(uint8_t addr_type,
                                       const uint8_t addr[6]) {
  if (addr == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  uint8_t blob[7];
  blob[0] = addr_type;
  memcpy(&blob[1], addr, 6);

  nvs_handle_t handle;
  esp_err_t err = nvs_open(SDF_STORAGE_NAMESPACE, NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    return err;
  }

  /* Compact the store so indices stay dense: remove the match and shift
   * every later record down one slot. */
  for (size_t i = 0; i < SDF_STORAGE_ADMISSION_MAX; i++) {
    char key[32];
    sdf_storage_admission_key(key, sizeof(key), i);

    uint8_t stored[sizeof(blob)];
    size_t len = sizeof(stored);
    err = nvs_get_blob(handle, key, stored, &len);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
      err = ESP_OK;
      break;
    }
    if (err != ESP_OK) {
      break;
    }

    if (len == sizeof(blob) && memcmp(stored, blob, sizeof(blob)) == 0) {
      /* Shift every later record down one slot; the vacated slot is the
       * last one that held a record before the shift. */
      size_t vacated = SDF_STORAGE_ADMISSION_MAX - 1;
      for (size_t j = i + 1; j < SDF_STORAGE_ADMISSION_MAX; j++) {
        char src_key[32];
        char dst_key[32];
        sdf_storage_admission_key(dst_key, sizeof(dst_key), j - 1);
        sdf_storage_admission_key(src_key, sizeof(src_key), j);

        uint8_t shift[sizeof(blob)];
        size_t shift_len = sizeof(shift);
        esp_err_t read_err =
            nvs_get_blob(handle, src_key, shift, &shift_len);
        if (read_err == ESP_ERR_NVS_NOT_FOUND) {
          vacated = j - 1;
          break;
        }
        if (read_err != ESP_OK) {
          err = read_err;
          break;
        }
        err = nvs_set_blob(handle, dst_key, shift, shift_len);
        if (err != ESP_OK) {
          break;
        }
      }
      if (err == ESP_OK) {
        char vacated_key[32];
        sdf_storage_admission_key(vacated_key, sizeof(vacated_key), vacated);
        esp_err_t erase_err = nvs_erase_key(handle, vacated_key);
        if (erase_err != ESP_OK && erase_err != ESP_ERR_NVS_NOT_FOUND) {
          err = erase_err;
        }
      }
      if (err == ESP_OK) {
        err = nvs_commit(handle);
      }
      break;
    }
  }

  nvs_close(handle);
  return err;
}

esp_err_t sdf_storage_admission_load_all(sdf_storage_admission_t *entries,
                                         size_t max_entries,
                                         size_t *count_out) {
  if (entries == NULL || count_out == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  nvs_handle_t handle;
  esp_err_t err = nvs_open(SDF_STORAGE_NAMESPACE, NVS_READONLY, &handle);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    /* Namespace never created - no admissions, not an error. */
    *count_out = 0;
    return ESP_OK;
  }
  if (err != ESP_OK) {
    return err;
  }

  size_t count = 0;
  err = ESP_OK;
  for (size_t i = 0; i < SDF_STORAGE_ADMISSION_MAX; i++) {
    char key[32];
    sdf_storage_admission_key(key, sizeof(key), i);

    uint8_t blob[7];
    size_t len = sizeof(blob);
    esp_err_t read_err = nvs_get_blob(handle, key, blob, &len);
    if (read_err == ESP_ERR_NVS_NOT_FOUND) {
      break; /* Dense store: first gap ends the scan. */
    }
    if (read_err != ESP_OK || len != sizeof(blob)) {
      ESP_LOGW(TAG, "admission_load_all: bad record at slot %u",
               (unsigned)i);
      continue;
    }

    if (count < max_entries) {
      entries[count].addr_type = blob[0];
      memcpy(entries[count].addr, &blob[1], 6);
    }
    count++;
  }

  nvs_close(handle);
  *count_out = count;
  return count > max_entries ? ESP_ERR_NO_MEM : ESP_OK;
}

esp_err_t sdf_storage_admission_clear_all(void) {
  nvs_handle_t handle;
  esp_err_t err = nvs_open(SDF_STORAGE_NAMESPACE, NVS_READWRITE, &handle);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    return ESP_OK;
  }
  if (err != ESP_OK) {
    return err;
  }

  for (size_t i = 0; i < SDF_STORAGE_ADMISSION_MAX; i++) {
    char key[32];
    sdf_storage_admission_key(key, sizeof(key), i);
    nvs_erase_key(handle, key);
  }

  err = nvs_commit(handle);
  nvs_close(handle);
  return err;
}

esp_err_t sdf_storage_erase_all(void) {
  esp_err_t err = nvs_flash_erase();
  if (err != ESP_OK) {
    return err;
  }

  err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
      err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    err = nvs_flash_erase();
    if (err != ESP_OK) {
      return err;
    }
    err = nvs_flash_init();
  }

  return err;
}
