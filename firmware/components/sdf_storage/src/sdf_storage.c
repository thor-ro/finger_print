#include "sdf_storage.h"

#include "esp_log.h"
#include "esp_partition.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#define SDF_STORAGE_NAMESPACE "nuki"
#define SDF_STORAGE_KEY_AUTH_ID "auth_id"
#define SDF_STORAGE_KEY_SHARED "shared_key"
#define SDF_STORAGE_KEY_BLE_HANDLES "ble_handles"

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
