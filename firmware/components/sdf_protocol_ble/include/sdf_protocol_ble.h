#ifndef SDF_PROTOCOL_BLE_H
#define SDF_PROTOCOL_BLE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define SDF_NUKI_NONCE_LEN 24
#define SDF_NUKI_CHALLENGE_NONCE_LEN 32
#define SDF_NUKI_SHARED_KEY_LEN 32
#define SDF_NUKI_NAME_SUFFIX_MAX 20

#define SDF_NUKI_MAX_PDATA 512
#define SDF_NUKI_MAX_MESSAGE (30 + 16 + SDF_NUKI_MAX_PDATA)

typedef enum {
    SDF_NUKI_RESULT_OK = 0,
    SDF_NUKI_RESULT_ERR_ARG = -1,
    SDF_NUKI_RESULT_ERR_NO_KEY = -2,
    SDF_NUKI_RESULT_ERR_CRYPTO = -3,
    SDF_NUKI_RESULT_ERR_CRC = -4,
    SDF_NUKI_RESULT_ERR_AUTH = -5,
    SDF_NUKI_RESULT_ERR_TOO_LARGE = -6,
    SDF_NUKI_RESULT_ERR_INCOMPLETE = -7
} sdf_nuki_result_t;

typedef enum {
    SDF_NUKI_CMD_REQUEST_DATA = 0x0001,
    SDF_NUKI_CMD_PUBLIC_KEY = 0x0003,
    SDF_NUKI_CMD_CHALLENGE = 0x0004,
    SDF_NUKI_CMD_AUTHORIZATION_AUTHENTICATOR = 0x0005,
    SDF_NUKI_CMD_AUTHORIZATION_DATA = 0x0006,
    SDF_NUKI_CMD_AUTHORIZATION_ID = 0x0007,
    SDF_NUKI_CMD_AUTHORIZATION_ID_CONFIRMATION = 0x001E,
    SDF_NUKI_CMD_KEYTURNER_STATES = 0x000C,
    SDF_NUKI_CMD_LOCK_ACTION = 0x000D,
    SDF_NUKI_CMD_STATUS = 0x000E,
    SDF_NUKI_CMD_SIMPLE_LOCK_ACTION = 0x0100,
    SDF_NUKI_CMD_AUTHORIZATION_INFO = 0x004C
} sdf_nuki_command_t;

typedef enum {
    SDF_NUKI_LOCK_ACTION_UNLOCK = 0x01,
    SDF_NUKI_LOCK_ACTION_LOCK = 0x02,
    SDF_NUKI_LOCK_ACTION_UNLATCH = 0x03,
    SDF_NUKI_LOCK_ACTION_LOCK_N_GO = 0x04,
    SDF_NUKI_LOCK_ACTION_LOCK_N_GO_UNLATCH = 0x05,
    SDF_NUKI_LOCK_ACTION_FULL_LOCK = 0x06
} sdf_nuki_lock_action_t;

typedef enum {
    SDF_NUKI_STATUS_COMPLETE = 0x00,
    SDF_NUKI_STATUS_ACCEPTED = 0x01
} sdf_nuki_status_t;

typedef struct {
    uint16_t command_id;
    const uint8_t *payload;
    size_t payload_len;
} sdf_nuki_message_t;

typedef int (*sdf_nuki_send_cb)(void *ctx, const uint8_t *data, size_t len);
typedef void (*sdf_nuki_message_cb)(void *ctx, const sdf_nuki_message_t *msg);

typedef struct {
    uint32_t authorization_id;
    uint32_t app_id;
    uint8_t shared_key[SDF_NUKI_SHARED_KEY_LEN];
} sdf_nuki_credentials_t;

typedef struct {
    sdf_nuki_credentials_t creds;
    sdf_nuki_send_cb send_encrypted_cb;
    void *send_encrypted_ctx;
    sdf_nuki_send_cb send_unencrypted_cb;
    void *send_unencrypted_ctx;
    sdf_nuki_message_cb message_cb;
    void *message_ctx;

    uint8_t rx_buf[SDF_NUKI_MAX_MESSAGE];
    size_t rx_len;
    size_t rx_expected;
    uint8_t pd_buf[SDF_NUKI_MAX_PDATA];
} sdf_nuki_client_t;

void sdf_protocol_ble_init(void);

int sdf_nuki_client_init(
    sdf_nuki_client_t *client,
    const sdf_nuki_credentials_t *creds,
    sdf_nuki_send_cb send_encrypted_cb,
    void *send_encrypted_ctx,
    sdf_nuki_send_cb send_unencrypted_cb,
    void *send_unencrypted_ctx,
    sdf_nuki_message_cb message_cb,
    void *message_ctx);

void sdf_nuki_client_reset_rx(sdf_nuki_client_t *client);

int sdf_nuki_client_send_unencrypted(
    sdf_nuki_client_t *client,
    uint16_t command,
    const uint8_t *payload,
    size_t payload_len);

int sdf_nuki_client_feed_encrypted(
    sdf_nuki_client_t *client,
    const uint8_t *data,
    size_t len);

int sdf_nuki_client_feed_encrypted_custom(
    sdf_nuki_client_t *client,
    uint32_t authorization_id,
    const uint8_t shared_key[SDF_NUKI_SHARED_KEY_LEN],
    const uint8_t *data,
    size_t len,
    sdf_nuki_message_cb message_cb,
    void *message_ctx);

int sdf_nuki_client_send_encrypted_custom(
    sdf_nuki_client_t *client,
    uint32_t authorization_id,
    const uint8_t shared_key[SDF_NUKI_SHARED_KEY_LEN],
    uint16_t command,
    const uint8_t *payload,
    size_t payload_len);

int sdf_nuki_client_send_request_data(
    sdf_nuki_client_t *client,
    uint16_t requested_command,
    const uint8_t *additional_data,
    size_t additional_len);

int sdf_nuki_client_send_lock_action(
    sdf_nuki_client_t *client,
    uint8_t lock_action,
    uint8_t flags,
    const uint8_t *name_suffix,
    size_t name_suffix_len,
    const uint8_t nonce_nk[SDF_NUKI_CHALLENGE_NONCE_LEN]);

int sdf_nuki_client_send_simple_lock_action(
    sdf_nuki_client_t *client,
    uint8_t lock_action,
    const uint8_t *name_suffix,
    size_t name_suffix_len,
    const uint8_t nonce_nk[SDF_NUKI_CHALLENGE_NONCE_LEN]);

int sdf_nuki_compute_shared_key(
    const uint8_t private_key[SDF_NUKI_SHARED_KEY_LEN],
    const uint8_t public_key[SDF_NUKI_SHARED_KEY_LEN],
    uint8_t shared_key_out[SDF_NUKI_SHARED_KEY_LEN]);

int sdf_nuki_compute_authenticator(
    const uint8_t *data,
    size_t data_len,
    const uint8_t key[SDF_NUKI_SHARED_KEY_LEN],
    uint8_t out[32]);

int sdf_nuki_parse_challenge(
    const sdf_nuki_message_t *msg,
    uint8_t nonce_nk[SDF_NUKI_CHALLENGE_NONCE_LEN]);

int sdf_nuki_parse_status(
    const sdf_nuki_message_t *msg,
    uint8_t *status_out);

#endif /* SDF_PROTOCOL_BLE_H */
