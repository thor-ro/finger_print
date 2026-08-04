#ifndef SDF_BLE_COMPANION_H
#define SDF_BLE_COMPANION_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "sdf_storage.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SDF_BLE_COMPANION_MAX_CONNECTIONS 3

typedef enum {
    SDF_BLE_COMPANION_AUTH_STATE_UNAUTHENTICATED = 0,
    SDF_BLE_COMPANION_AUTH_STATE_PENDING = 1,
    SDF_BLE_COMPANION_AUTH_STATE_AUTHENTICATED = 2,
} sdf_ble_companion_auth_state_t;

typedef struct {
    uint16_t conn_handle;
    bool connected;
    sdf_ble_companion_auth_state_t auth_state;
    bool auth_pending;
    char username[SDF_STORAGE_WEB_USER_NAME_MAX];
    uint8_t password_hash[SDF_STORAGE_WEB_USER_HASH_LEN];
} sdf_ble_companion_connection_t;

typedef void (*sdf_ble_companion_auth_request_cb)(void *ctx,
                                                   const char *username,
                                                   const uint8_t *password_hash,
                                                   size_t hash_len);

typedef void (*sdf_ble_companion_config_write_cb)(void *ctx,
                                                   const uint8_t *data,
                                                   size_t len);

typedef void (*sdf_ble_companion_enroll_write_cb)(void *ctx,
                                                   const uint8_t *data,
                                                   size_t len);

typedef void (*sdf_ble_companion_ota_write_cb)(void *ctx,
                                                const uint8_t *data,
                                                size_t len);


typedef struct {
    void *ctx;
    sdf_ble_companion_auth_request_cb on_auth_request;
    sdf_ble_companion_config_write_cb on_config_write;
    sdf_ble_companion_enroll_write_cb on_enroll_write;
    sdf_ble_companion_ota_write_cb on_ota_write;
} sdf_ble_companion_callbacks_t;

esp_err_t sdf_ble_companion_init(const sdf_ble_companion_callbacks_t *callbacks);
esp_err_t sdf_ble_companion_deinit(void);

bool sdf_ble_companion_is_authenticated(uint16_t conn_handle);
esp_err_t sdf_ble_companion_set_authenticated(uint16_t conn_handle, bool authenticated);
esp_err_t sdf_ble_companion_reply_auth(const char *username, bool authorized);

esp_err_t sdf_ble_companion_notify_config(uint16_t conn_handle, const uint8_t *data, size_t len);
esp_err_t sdf_ble_companion_notify_enroll(uint16_t conn_handle, const uint8_t *data, size_t len);
esp_err_t sdf_ble_companion_notify_ota(uint16_t conn_handle, const uint8_t *data, size_t len);

/**
 * Broadcast data to all authenticated connections.
 * Iterates over all connections and sends notifications to authenticated ones.
 */
esp_err_t sdf_ble_companion_broadcast_ota(const uint8_t *data, size_t len);

/**
 * Validate and start an HTTPS OTA request encoded as UTF-8 JSON:
 * {"ssid":"...","password":"...","firmwareUrl":"https://..."}.
 */
esp_err_t sdf_ble_companion_start_ota_request(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* SDF_BLE_COMPANION_H */
