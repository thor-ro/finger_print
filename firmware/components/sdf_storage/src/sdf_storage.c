#include "sdf_storage.h"

#include "nvs.h"
#include "nvs_flash.h"

#define SDF_STORAGE_NAMESPACE "nuki"
#define SDF_STORAGE_KEY_AUTH_ID "auth_id"
#define SDF_STORAGE_KEY_SHARED "shared_key"

esp_err_t sdf_storage_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        esp_err_t erase_err = nvs_flash_erase();
        if (erase_err != ESP_OK) {
            return erase_err;
        }
        err = nvs_flash_init();
    }

    return err;
}

esp_err_t sdf_storage_nuki_save(uint32_t authorization_id, const uint8_t shared_key[32])
{
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

esp_err_t sdf_storage_nuki_load(uint32_t *authorization_id, uint8_t shared_key[32])
{
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

esp_err_t sdf_storage_nuki_clear(void)
{
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
        err = nvs_commit(handle);
    }

    nvs_close(handle);
    return err;
}
