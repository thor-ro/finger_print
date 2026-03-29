#ifndef SDF_NUKI_PAIRING_H
#define SDF_NUKI_PAIRING_H

#include <stddef.h>
#include <stdint.h>

#include "sdf_protocol_ble.h"

#define SDF_NUKI_PAIRING_AUTH_ID 0x7FFFFFFFu

typedef enum {
    SDF_NUKI_PAIRING_IDLE = 0,
    SDF_NUKI_PAIRING_WAIT_PUBLIC_KEY,
    SDF_NUKI_PAIRING_WAIT_CHALLENGE,
    SDF_NUKI_PAIRING_WAIT_AUTHORIZATION_DATA,
    SDF_NUKI_PAIRING_WAIT_AUTHORIZATION_ID,
    SDF_NUKI_PAIRING_COMPLETE,
    SDF_NUKI_PAIRING_ERROR
} sdf_nuki_pairing_state_t;

typedef struct {
    sdf_nuki_client_t *client;
    sdf_nuki_pairing_state_t state;
    uint8_t id_type;
    uint32_t app_id;
    uint8_t name[32];

    uint8_t private_key[SDF_NUKI_SHARED_KEY_LEN];
    uint8_t public_key[SDF_NUKI_SHARED_KEY_LEN];
    uint8_t smartlock_public_key[SDF_NUKI_SHARED_KEY_LEN];

    uint8_t nonce_nk[SDF_NUKI_CHALLENGE_NONCE_LEN];
    uint8_t nonce_na[SDF_NUKI_CHALLENGE_NONCE_LEN];
    uint8_t shared_key[SDF_NUKI_SHARED_KEY_LEN];

    uint32_t authorization_id;
    uint8_t uuid[16];

    uint8_t rx_buf[SDF_NUKI_MAX_MESSAGE];
    size_t rx_len;
    size_t rx_expected;
} sdf_nuki_pairing_t;

int sdf_nuki_pairing_init(
    sdf_nuki_pairing_t *pairing,
    sdf_nuki_client_t *client,
    uint8_t id_type,
    uint32_t app_id,
    const char *name);

int sdf_nuki_pairing_start(sdf_nuki_pairing_t *pairing);

int sdf_nuki_pairing_handle_unencrypted(
    sdf_nuki_pairing_t *pairing,
    const uint8_t *data,
    size_t len);

int sdf_nuki_pairing_handle_encrypted(
    sdf_nuki_pairing_t *pairing,
    const uint8_t *data,
    size_t len);

int sdf_nuki_pairing_get_credentials(
    const sdf_nuki_pairing_t *pairing,
    sdf_nuki_credentials_t *creds_out);

#endif /* SDF_NUKI_PAIRING_H */
