#include "sdf_platform_nvs.h"

#ifndef CONFIG_IDF_TARGET_LINUX
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_partition.h"
#include "esp_log.h"
#else
#include "sdf_mock_linux_nvs.h"
#endif

static const char *TAG = "sdf_platform_nvs";
static bool s_nvs_initialized = false;
static sdf_platform_nvs_security_status_t s_security_status = {0};

esp_err_t sdf_platform_nvs_init(bool require_encrypted) {
    if (s_nvs_initialized) {
        return ESP_OK;
    }

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition truncated, erasing...");
        err = nvs_flash_erase();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to erase NVS: %s", esp_err_to_name(err));
            return err;
        }
        err = nvs_flash_init();
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(err));
        return err;
    }

    // Check NVS security status
    const esp_partition_t *keys_partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS_KEYS, NULL);

    s_security_status.nvs_keys_partition_present = (keys_partition != NULL);
    s_security_status.nvs_keys_accessible = false;
    s_security_status.nvs_encryption_enabled = false;
    s_security_status.require_encrypted_nvs = require_encrypted;

    if (keys_partition != NULL) {
        nvs_sec_cfg_t cfg = {0};
        err = nvs_flash_read_security_cfg(keys_partition, &cfg);
        if (err == ESP_OK) {
            s_security_status.nvs_keys_accessible = true;
            s_security_status.nvs_encryption_enabled = true;
        } else if (err == ESP_ERR_NVS_KEYS_NOT_INITIALIZED || err == ESP_ERR_NVS_CORRUPT_KEY_PART) {
            ESP_LOGW(TAG, "NVS keys not initialized, generating...");
            err = nvs_flash_generate_keys(keys_partition, &cfg);
            if (err == ESP_OK) {
                s_security_status.nvs_keys_accessible = true;
                s_security_status.nvs_encryption_enabled = true;
            } else {
                ESP_LOGE(TAG, "Failed to generate NVS keys: %s", esp_err_to_name(err));
            }
        } else if (err == ESP_ERR_NVS_WRONG_ENCRYPTION) {
            ESP_LOGE(TAG, "NVS encryption configuration mismatch");
        }
    }

    if (require_encrypted && !s_security_status.nvs_encryption_enabled) {
        ESP_LOGE(TAG, "Encrypted NVS required but not available");
        nvs_flash_deinit();
        return ESP_ERR_NOT_SUPPORTED;
    }

    s_nvs_initialized = true;
    ESP_LOGI(TAG, "NVS initialized (encrypted=%d, keys_partition=%d, keys_accessible=%d)",
             s_security_status.nvs_encryption_enabled,
             s_security_status.nvs_keys_partition_present,
             s_security_status.nvs_keys_accessible);

    return ESP_OK;
}

esp_err_t sdf_platform_nvs_get_security_status(sdf_platform_nvs_security_status_t *status) {
    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *status = s_security_status;
    return ESP_OK;
}

bool sdf_platform_nvs_security_ok(void) {
    if (!s_security_status.require_encrypted_nvs) {
        return true;
    }
    return s_security_status.nvs_encryption_enabled &&
           s_security_status.nvs_keys_partition_present &&
           s_security_status.nvs_keys_accessible;
}

esp_err_t sdf_platform_nvs_erase_all(void) {
    if (!s_nvs_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    return nvs_flash_erase();
}

void sdf_platform_nvs_deinit(void) {
    if (s_nvs_initialized) {
        nvs_flash_deinit();
        s_nvs_initialized = false;
    }
}