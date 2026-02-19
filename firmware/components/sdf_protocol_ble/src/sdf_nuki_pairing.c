#include "sdf_nuki_pairing.h"

#include <string.h>

#include "esp_random.h"
#include "sdf_nuki_crypto.h"

static uint16_t sdf_nuki_le16_read(const uint8_t *src)
{
    return (uint16_t)src[0] | ((uint16_t)src[1] << 8);
}

static void sdf_nuki_le16_write(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)(value & 0xFF);
    dst[1] = (uint8_t)((value >> 8) & 0xFF);
}

static uint32_t sdf_nuki_le32_read(const uint8_t *src)
{
    return (uint32_t)src[0]
        | ((uint32_t)src[1] << 8)
        | ((uint32_t)src[2] << 16)
        | ((uint32_t)src[3] << 24);
}

static void sdf_nuki_le32_write(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)(value & 0xFF);
    dst[1] = (uint8_t)((value >> 8) & 0xFF);
    dst[2] = (uint8_t)((value >> 16) & 0xFF);
    dst[3] = (uint8_t)((value >> 24) & 0xFF);
}

static uint16_t sdf_nuki_crc_ccitt(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;

    for (size_t i = 0; i < len; ++i) {
        crc ^= (uint16_t)data[i] << 8;
        for (int bit = 0; bit < 8; ++bit) {
            if (crc & 0x8000) {
                crc = (uint16_t)((crc << 1) ^ 0x1021);
            } else {
                crc = (uint16_t)(crc << 1);
            }
        }
    }

    return crc;
}

static void sdf_nuki_random(uint8_t *out, size_t len)
{
    esp_fill_random(out, len);
}

static size_t sdf_nuki_unencrypted_expected_length(uint16_t command)
{
    switch (command) {
    case SDF_NUKI_CMD_PUBLIC_KEY:
        return 2 + 32 + 2;
    case SDF_NUKI_CMD_CHALLENGE:
        return 2 + 32 + 2;
    case SDF_NUKI_CMD_AUTHORIZATION_INFO:
        return 2 + 1 + 2;
    default:
        return 0;
    }
}

static int sdf_nuki_parse_unencrypted(
    const uint8_t *buf,
    size_t len,
    sdf_nuki_message_t *msg)
{
    if (buf == NULL || msg == NULL) {
        return SDF_NUKI_RESULT_ERR_ARG;
    }

    if (len < 4) {
        return SDF_NUKI_RESULT_ERR_INCOMPLETE;
    }

    uint16_t crc_expected = sdf_nuki_le16_read(buf + len - 2);
    uint16_t crc_actual = sdf_nuki_crc_ccitt(buf, len - 2);
    if (crc_expected != crc_actual) {
        return SDF_NUKI_RESULT_ERR_CRC;
    }

    msg->command_id = sdf_nuki_le16_read(buf);
    msg->payload = buf + 2;
    msg->payload_len = len - 4;

    return SDF_NUKI_RESULT_OK;
}

static int sdf_nuki_generate_keypair(uint8_t priv[SDF_NUKI_SHARED_KEY_LEN], uint8_t pub[SDF_NUKI_SHARED_KEY_LEN])
{
    static const uint8_t basepoint[32] = {9};

    if (priv == NULL || pub == NULL) {
        return SDF_NUKI_RESULT_ERR_ARG;
    }

    sdf_nuki_random(priv, SDF_NUKI_SHARED_KEY_LEN);
    if (crypto_scalarmult(pub, priv, basepoint) != 0) {
        return SDF_NUKI_RESULT_ERR_CRYPTO;
    }

    return SDF_NUKI_RESULT_OK;
}

static int sdf_nuki_pairing_send_authorization_authenticator(sdf_nuki_pairing_t *pairing)
{
    uint8_t data[32 + 32 + 32];
    memcpy(data, pairing->public_key, 32);
    memcpy(data + 32, pairing->smartlock_public_key, 32);
    memcpy(data + 64, pairing->nonce_nk, 32);

    uint8_t authenticator[32];
    int res = sdf_nuki_compute_authenticator(data, sizeof(data), pairing->shared_key, authenticator);
    if (res != SDF_NUKI_RESULT_OK) {
        return res;
    }

    uint8_t payload[64];
    memcpy(payload, authenticator, 32);
    memcpy(payload + 32, pairing->public_key, 32);

    return sdf_nuki_client_send_unencrypted(
        pairing->client,
        SDF_NUKI_CMD_AUTHORIZATION_AUTHENTICATOR,
        payload,
        sizeof(payload));
}

static int sdf_nuki_pairing_send_authorization_data(sdf_nuki_pairing_t *pairing)
{
    sdf_nuki_random(pairing->nonce_na, sizeof(pairing->nonce_na));

    uint8_t auth_input[1 + 4 + 32 + 32 + 32];
    size_t offset = 0;
    auth_input[offset++] = pairing->id_type;
    sdf_nuki_le32_write(auth_input + offset, pairing->app_id);
    offset += 4;
    memcpy(auth_input + offset, pairing->name, sizeof(pairing->name));
    offset += sizeof(pairing->name);
    memcpy(auth_input + offset, pairing->nonce_na, sizeof(pairing->nonce_na));
    offset += sizeof(pairing->nonce_na);
    memcpy(auth_input + offset, pairing->nonce_nk, sizeof(pairing->nonce_nk));
    offset += sizeof(pairing->nonce_nk);

    uint8_t authenticator[32];
    int res = sdf_nuki_compute_authenticator(auth_input, offset, pairing->shared_key, authenticator);
    if (res != SDF_NUKI_RESULT_OK) {
        return res;
    }

    uint8_t payload[32 + 1 + 4 + 32 + 32 + 32];
    offset = 0;
    memcpy(payload + offset, authenticator, 32);
    offset += 32;
    payload[offset++] = pairing->id_type;
    sdf_nuki_le32_write(payload + offset, pairing->app_id);
    offset += 4;
    memcpy(payload + offset, pairing->name, sizeof(pairing->name));
    offset += sizeof(pairing->name);
    memcpy(payload + offset, pairing->nonce_na, sizeof(pairing->nonce_na));
    offset += sizeof(pairing->nonce_na);
    memcpy(payload + offset, pairing->nonce_nk, sizeof(pairing->nonce_nk));
    offset += sizeof(pairing->nonce_nk);

    return sdf_nuki_client_send_encrypted_custom(
        pairing->client,
        SDF_NUKI_PAIRING_AUTH_ID,
        pairing->shared_key,
        SDF_NUKI_CMD_AUTHORIZATION_DATA,
        payload,
        offset);
}

static int sdf_nuki_pairing_send_authorization_id_confirmation(sdf_nuki_pairing_t *pairing)
{
    uint8_t auth_input[4 + 32];
    sdf_nuki_le32_write(auth_input, pairing->authorization_id);
    memcpy(auth_input + 4, pairing->nonce_nk, sizeof(pairing->nonce_nk));

    uint8_t authenticator[32];
    int res = sdf_nuki_compute_authenticator(auth_input, sizeof(auth_input), pairing->shared_key, authenticator);
    if (res != SDF_NUKI_RESULT_OK) {
        return res;
    }

    uint8_t payload[32 + 4 + 32];
    memcpy(payload, authenticator, 32);
    sdf_nuki_le32_write(payload + 32, pairing->authorization_id);
    memcpy(payload + 36, pairing->nonce_nk, sizeof(pairing->nonce_nk));

    return sdf_nuki_client_send_encrypted_custom(
        pairing->client,
        SDF_NUKI_PAIRING_AUTH_ID,
        pairing->shared_key,
        SDF_NUKI_CMD_AUTHORIZATION_ID_CONFIRMATION,
        payload,
        sizeof(payload));
}

int sdf_nuki_pairing_init(
    sdf_nuki_pairing_t *pairing,
    sdf_nuki_client_t *client,
    uint8_t id_type,
    uint32_t app_id,
    const char *name)
{
    if (pairing == NULL || client == NULL || name == NULL) {
        return SDF_NUKI_RESULT_ERR_ARG;
    }

    memset(pairing, 0, sizeof(*pairing));
    pairing->client = client;
    pairing->state = SDF_NUKI_PAIRING_IDLE;
    pairing->id_type = id_type;
    pairing->app_id = app_id;
    memset(pairing->name, 0, sizeof(pairing->name));
    strncpy((char *)pairing->name, name, sizeof(pairing->name));

    return sdf_nuki_generate_keypair(pairing->private_key, pairing->public_key);
}

int sdf_nuki_pairing_start(sdf_nuki_pairing_t *pairing)
{
    if (pairing == NULL || pairing->client == NULL) {
        return SDF_NUKI_RESULT_ERR_ARG;
    }

    uint8_t payload[2];
    sdf_nuki_le16_write(payload, SDF_NUKI_CMD_PUBLIC_KEY);

    int res = sdf_nuki_client_send_unencrypted(
        pairing->client,
        SDF_NUKI_CMD_REQUEST_DATA,
        payload,
        sizeof(payload));
    if (res == SDF_NUKI_RESULT_OK) {
        pairing->state = SDF_NUKI_PAIRING_WAIT_PUBLIC_KEY;
    }

    return res;
}

int sdf_nuki_pairing_handle_unencrypted(
    sdf_nuki_pairing_t *pairing,
    const uint8_t *data,
    size_t len)
{
    if (pairing == NULL || data == NULL || len == 0) {
        return SDF_NUKI_RESULT_ERR_ARG;
    }

    if (pairing->rx_len + len > sizeof(pairing->rx_buf)) {
        pairing->rx_len = 0;
        pairing->rx_expected = 0;
        return SDF_NUKI_RESULT_ERR_TOO_LARGE;
    }

    memcpy(pairing->rx_buf + pairing->rx_len, data, len);
    pairing->rx_len += len;

    if (pairing->rx_expected == 0) {
        if (pairing->rx_len < 2) {
            return SDF_NUKI_RESULT_ERR_INCOMPLETE;
        }

        uint16_t command = sdf_nuki_le16_read(pairing->rx_buf);
        pairing->rx_expected = sdf_nuki_unencrypted_expected_length(command);
        if (pairing->rx_expected == 0) {
            pairing->rx_len = 0;
            return SDF_NUKI_RESULT_ERR_ARG;
        }
    }

    if (pairing->rx_len < pairing->rx_expected) {
        return SDF_NUKI_RESULT_ERR_INCOMPLETE;
    }

    if (pairing->rx_len > pairing->rx_expected) {
        pairing->rx_len = 0;
        pairing->rx_expected = 0;
        return SDF_NUKI_RESULT_ERR_ARG;
    }

    sdf_nuki_message_t msg;
    int res = sdf_nuki_parse_unencrypted(pairing->rx_buf, pairing->rx_len, &msg);
    pairing->rx_len = 0;
    pairing->rx_expected = 0;
    if (res != SDF_NUKI_RESULT_OK) {
        return res;
    }

    if (msg.command_id == SDF_NUKI_CMD_PUBLIC_KEY && msg.payload_len == 32) {
        memcpy(pairing->smartlock_public_key, msg.payload, 32);
        res = sdf_nuki_compute_shared_key(
            pairing->private_key,
            pairing->smartlock_public_key,
            pairing->shared_key);
        if (res != SDF_NUKI_RESULT_OK) {
            pairing->state = SDF_NUKI_PAIRING_ERROR;
            return res;
        }

        res = sdf_nuki_client_send_unencrypted(
            pairing->client,
            SDF_NUKI_CMD_PUBLIC_KEY,
            pairing->public_key,
            sizeof(pairing->public_key));
        if (res != SDF_NUKI_RESULT_OK) {
            pairing->state = SDF_NUKI_PAIRING_ERROR;
            return res;
        }

        pairing->state = SDF_NUKI_PAIRING_WAIT_CHALLENGE;
        return SDF_NUKI_RESULT_OK;
    }

    if (msg.command_id == SDF_NUKI_CMD_CHALLENGE && msg.payload_len == 32) {
        memcpy(pairing->nonce_nk, msg.payload, 32);

        res = sdf_nuki_pairing_send_authorization_authenticator(pairing);
        if (res != SDF_NUKI_RESULT_OK) {
            pairing->state = SDF_NUKI_PAIRING_ERROR;
            return res;
        }

        res = sdf_nuki_pairing_send_authorization_data(pairing);
        if (res != SDF_NUKI_RESULT_OK) {
            pairing->state = SDF_NUKI_PAIRING_ERROR;
            return res;
        }

        pairing->state = SDF_NUKI_PAIRING_WAIT_AUTHORIZATION_ID;
        return SDF_NUKI_RESULT_OK;
    }

    if (msg.command_id == SDF_NUKI_CMD_AUTHORIZATION_INFO) {
        return SDF_NUKI_RESULT_OK;
    }

    return SDF_NUKI_RESULT_ERR_ARG;
}

static void sdf_nuki_pairing_on_message(void *ctx, const sdf_nuki_message_t *msg)
{
    sdf_nuki_pairing_t *pairing = (sdf_nuki_pairing_t *)ctx;
    if (pairing == NULL || msg == NULL) {
        return;
    }

    if (msg->command_id != SDF_NUKI_CMD_AUTHORIZATION_ID) {
        return;
    }

    if (msg->payload_len >= 32 + 4 + 16 + 32 + 32) {
        const uint8_t *auth = msg->payload;
        uint32_t auth_id = sdf_nuki_le32_read(msg->payload + 32);
        const uint8_t *uuid = msg->payload + 36;
        const uint8_t *nonce_nk = msg->payload + 52;
        const uint8_t *nonce_na = msg->payload + 84;

        uint8_t auth_input[4 + 16 + 32 + 32];
        sdf_nuki_le32_write(auth_input, auth_id);
        memcpy(auth_input + 4, uuid, 16);
        memcpy(auth_input + 20, nonce_nk, 32);
        memcpy(auth_input + 52, nonce_na, 32);

        uint8_t expected[32];
        if (sdf_nuki_compute_authenticator(auth_input, sizeof(auth_input), pairing->shared_key, expected) != SDF_NUKI_RESULT_OK) {
            pairing->state = SDF_NUKI_PAIRING_ERROR;
            return;
        }

        if (memcmp(expected, auth, 32) != 0) {
            pairing->state = SDF_NUKI_PAIRING_ERROR;
            return;
        }

        pairing->authorization_id = auth_id;
        memcpy(pairing->uuid, uuid, sizeof(pairing->uuid));
    } else if (msg->payload_len >= 4 + 16) {
        pairing->authorization_id = sdf_nuki_le32_read(msg->payload);
        memcpy(pairing->uuid, msg->payload + 4, sizeof(pairing->uuid));
    } else {
        pairing->state = SDF_NUKI_PAIRING_ERROR;
        return;
    }

    if (sdf_nuki_pairing_send_authorization_id_confirmation(pairing) == SDF_NUKI_RESULT_OK) {
        pairing->state = SDF_NUKI_PAIRING_COMPLETE;
    } else {
        pairing->state = SDF_NUKI_PAIRING_ERROR;
    }
}

int sdf_nuki_pairing_handle_encrypted(
    sdf_nuki_pairing_t *pairing,
    const uint8_t *data,
    size_t len)
{
    if (pairing == NULL || data == NULL || len == 0) {
        return SDF_NUKI_RESULT_ERR_ARG;
    }

    return sdf_nuki_client_feed_encrypted_custom(
        pairing->client,
        SDF_NUKI_PAIRING_AUTH_ID,
        pairing->shared_key,
        data,
        len,
        sdf_nuki_pairing_on_message,
        pairing);
}

int sdf_nuki_pairing_get_credentials(
    const sdf_nuki_pairing_t *pairing,
    sdf_nuki_credentials_t *creds_out)
{
    if (pairing == NULL || creds_out == NULL) {
        return SDF_NUKI_RESULT_ERR_ARG;
    }

    if (pairing->state != SDF_NUKI_PAIRING_COMPLETE) {
        return SDF_NUKI_RESULT_ERR_INCOMPLETE;
    }

    memset(creds_out, 0, sizeof(*creds_out));
    creds_out->authorization_id = pairing->authorization_id;
    creds_out->app_id = pairing->app_id;
    memcpy(creds_out->shared_key, pairing->shared_key, sizeof(pairing->shared_key));

    return SDF_NUKI_RESULT_OK;
}
