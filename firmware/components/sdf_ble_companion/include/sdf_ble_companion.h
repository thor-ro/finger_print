#ifndef SDF_BLE_COMPANION_H
#define SDF_BLE_COMPANION_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "sdf_services.h"
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
    uint8_t auth_value[512];
    uint16_t auth_value_len;
    uint8_t config_value[512];
    uint16_t config_value_len;
    uint8_t enroll_value[512];
    uint16_t enroll_value_len;
    uint8_t ota_value[512];
    uint16_t ota_value_len;
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

/**
 * Fired when an already-authenticated GATT client requests one of the
 * admin-fingerprint-gated actions reachable over BLE - Nuki re-pair,
 * Enroll-Admin, or Zigbee Join (a `{"action":"nuki_repair"|"enroll_admin"|
 * "zb_join"}` write on the Config characteristic; nuki_repair is only
 * accepted once setup is already complete - see
 * sdf_ble_companion_config_access()). Parallel to on_auth_request: the
 * originating connection handle is passed through so the result can later
 * be routed back to it via sdf_ble_companion_reply_admin_action().
 */
typedef void (*sdf_ble_companion_admin_action_request_cb)(void *ctx,
                                                            sdf_services_admin_action_t action,
                                                            uint16_t conn_handle);

/**
 * Returns true if the write was well-formed and accepted per the BLE OTA
 * chunked-transfer wire format (regardless of whether the underlying OTA
 * operation ultimately succeeds - such failures are reported asynchronously
 * via sdf_ble_companion_notify_ota()), or false if the write was malformed
 * or out-of-protocol and must be rejected by the GATT layer (non-zero ATT
 * error, no notify) without opening, resuming, or corrupting any session.
 */
typedef bool (*sdf_ble_companion_ota_write_cb)(void *ctx,
                                                uint16_t conn_handle,
                                                const uint8_t *data,
                                                size_t len);


typedef struct {
    void *ctx;
    sdf_ble_companion_auth_request_cb on_auth_request;
    sdf_ble_companion_config_write_cb on_config_write;
    sdf_ble_companion_enroll_write_cb on_enroll_write;
    sdf_ble_companion_ota_write_cb on_ota_write;
    sdf_ble_companion_admin_action_request_cb on_admin_action_request;
} sdf_ble_companion_callbacks_t;

esp_err_t sdf_ble_companion_init(const sdf_ble_companion_callbacks_t *callbacks);
esp_err_t sdf_ble_companion_deinit(void);

bool sdf_ble_companion_is_authenticated(uint16_t conn_handle);
esp_err_t sdf_ble_companion_set_authenticated(uint16_t conn_handle, bool authenticated);
esp_err_t sdf_ble_companion_reply_auth(const char *username, bool authorized);

/**
 * Routes the result of a pending BLE-triggered admin action request (Nuki
 * re-pair, Enroll-Admin, or Zigbee Join) back to the originating connection
 * (identified by conn_handle, unlike sdf_ble_companion_reply_auth() which is
 * keyed by username) as a `{"nuki_repair"|"enroll_admin"|"zb_join":
 * true|false}` notification on the Config characteristic. Returns
 * ESP_ERR_INVALID_ARG if `action` isn't one of those three, or
 * ESP_ERR_INVALID_STATE if the connection is no longer connected or
 * authenticated - the caller has no further recovery action to take in
 * either case, the client that would have received the reply is simply gone
 * (or was never a valid target).
 */
esp_err_t sdf_ble_companion_reply_admin_action(uint16_t conn_handle,
                                                sdf_services_admin_action_t action,
                                                bool authorized);

esp_err_t sdf_ble_companion_notify_config(uint16_t conn_handle, const uint8_t *data, size_t len);
esp_err_t sdf_ble_companion_notify_enroll(uint16_t conn_handle, const uint8_t *data, size_t len);
esp_err_t sdf_ble_companion_notify_ota(uint16_t conn_handle, const uint8_t *data, size_t len);

/**
 * Broadcast data to all authenticated connections.
 * Iterates over all connections and sends notifications to authenticated ones.
 */
esp_err_t sdf_ble_companion_broadcast_ota(const uint8_t *data, size_t len);

/**
 * Handle a write to the OTA characteristic implementing the chunked binary
 * firmware transfer protocol over the existing authenticated BLE GATT
 * connection: opcode-prefixed control/data messages, `[opcode:1][payload...]`
 *   - 0x01 BEGIN: 4-byte little-endian uint32 declared image size (5 bytes
 *     total). Opens a new OTA session, or - if a session with a matching
 *     declared size is already open - resumes it by reporting the current
 *     byte offset.
 *   - 0x02 CHUNK: raw firmware bytes (1..max_chunk_len, where
 *     max_chunk_len = ble_att_mtu(conn_handle) - 4).
 *   - 0x03 END: no payload (1 byte total). Triggers integrity/signature
 *     verification and commit.
 * Per-chunk and completion status are reported back via
 * sdf_ble_companion_notify_ota(). Intended to be wired up as the
 * `on_ota_write` callback in sdf_ble_companion_callbacks_t.
 */
bool sdf_ble_companion_handle_ota_write(void *ctx, uint16_t conn_handle,
                                         const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* SDF_BLE_COMPANION_H */
