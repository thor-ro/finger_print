#include "sdf_protocol_ble.h"

#include <string.h>

#ifndef CONFIG_IDF_TARGET_LINUX

#include "esp_random.h"
#include "mbedtls/md.h"
#include "sdkconfig.h"

#include "sdf_nuki_crypto.h"

#define SDF_NUKI_ADATA_LEN 30
#define SDF_NUKI_PDATA_HEADER_LEN 6
#define SDF_NUKI_SECRETBOX_OVERHEAD 16

#if CONFIG_SDF_SECURITY_NONCE_REPLAY_WINDOW > SDF_NUKI_NONCE_CACHE_MAX
#define SDF_NUKI_NONCE_REPLAY_WINDOW SDF_NUKI_NONCE_CACHE_MAX
#else
#define SDF_NUKI_NONCE_REPLAY_WINDOW CONFIG_SDF_SECURITY_NONCE_REPLAY_WINDOW
#endif

static uint16_t sdf_nuki_le16_read(const uint8_t *src) {
  return (uint16_t)src[0] | ((uint16_t)src[1] << 8);
}

static void sdf_nuki_le16_write(uint8_t *dst, uint16_t value) {
  dst[0] = (uint8_t)(value & 0xFF);
  dst[1] = (uint8_t)((value >> 8) & 0xFF);
}

static uint32_t sdf_nuki_le32_read(const uint8_t *src) {
  return (uint32_t)src[0] | ((uint32_t)src[1] << 8) | ((uint32_t)src[2] << 16) |
         ((uint32_t)src[3] << 24);
}

static void sdf_nuki_le32_write(uint8_t *dst, uint32_t value) {
  dst[0] = (uint8_t)(value & 0xFF);
  dst[1] = (uint8_t)((value >> 8) & 0xFF);
  dst[2] = (uint8_t)((value >> 16) & 0xFF);
  dst[3] = (uint8_t)((value >> 24) & 0xFF);
}

static void sdf_nuki_le64_write(uint8_t *dst, uint64_t value) {
  for (size_t i = 0; i < 8; ++i) {
    dst[i] = (uint8_t)((value >> (8 * i)) & 0xFF);
  }
}

static uint16_t sdf_nuki_crc_ccitt(const uint8_t *data, size_t len) {
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

static int sdf_nuki_build_unencrypted_message(uint16_t command,
                                              const uint8_t *payload,
                                              size_t payload_len, uint8_t *out,
                                              size_t out_cap, size_t *out_len) {
  if (out == NULL || out_len == NULL) {
    return SDF_NUKI_RESULT_ERR_ARG;
  }

  size_t total_len = 2 + payload_len + 2;
  if (total_len > out_cap) {
    return SDF_NUKI_RESULT_ERR_TOO_LARGE;
  }

  sdf_nuki_le16_write(out, command);
  if (payload_len > 0 && payload != NULL) {
    memcpy(out + 2, payload, payload_len);
  }

  uint16_t crc = sdf_nuki_crc_ccitt(out, 2 + payload_len);
  sdf_nuki_le16_write(out + 2 + payload_len, crc);

  *out_len = total_len;
  return SDF_NUKI_RESULT_OK;
}

static void sdf_nuki_random(uint8_t *out, size_t len) {
  esp_fill_random(out, len);
}

static int
sdf_nuki_secretbox_encrypt(const uint8_t *plaintext, size_t plaintext_len,
                           const uint8_t nonce[SDF_NUKI_NONCE_LEN],
                           const uint8_t key[SDF_NUKI_SHARED_KEY_LEN],
                           uint8_t *ciphertext, size_t ciphertext_cap,
                           size_t *ciphertext_len) {
  if (ciphertext_cap < plaintext_len + SDF_NUKI_SECRETBOX_OVERHEAD) {
    return SDF_NUKI_RESULT_ERR_TOO_LARGE;
  }

  uint8_t m[SDF_NUKI_MAX_PDATA + 32];
  uint8_t c[SDF_NUKI_MAX_PDATA + 32];
  size_t mlen = plaintext_len + 32;

  if (mlen > sizeof(m)) {
    return SDF_NUKI_RESULT_ERR_TOO_LARGE;
  }

  memset(m, 0, 32);
  if (plaintext_len > 0) {
    memcpy(m + 32, plaintext, plaintext_len);
  }

  if (crypto_secretbox(c, m, (unsigned long long)mlen, nonce, key) != 0) {
    return SDF_NUKI_RESULT_ERR_CRYPTO;
  }

  memcpy(ciphertext, c + 16, plaintext_len + 16);
  *ciphertext_len = plaintext_len + 16;

  return SDF_NUKI_RESULT_OK;
}

static int
sdf_nuki_secretbox_decrypt(const uint8_t *ciphertext, size_t ciphertext_len,
                           const uint8_t nonce[SDF_NUKI_NONCE_LEN],
                           const uint8_t key[SDF_NUKI_SHARED_KEY_LEN],
                           uint8_t *plaintext, size_t plaintext_cap,
                           size_t *plaintext_len) {
  if (ciphertext_len < SDF_NUKI_SECRETBOX_OVERHEAD) {
    return SDF_NUKI_RESULT_ERR_ARG;
  }

  size_t mlen = ciphertext_len + 16;
  if (mlen > plaintext_cap + 32) {
    return SDF_NUKI_RESULT_ERR_TOO_LARGE;
  }

  uint8_t c[SDF_NUKI_MAX_PDATA + 32];
  uint8_t m[SDF_NUKI_MAX_PDATA + 32];

  if (mlen > sizeof(c) || mlen > sizeof(m)) {
    return SDF_NUKI_RESULT_ERR_TOO_LARGE;
  }

  memset(c, 0, 16);
  memcpy(c + 16, ciphertext, ciphertext_len);

  if (crypto_secretbox_open(m, c, (unsigned long long)mlen, nonce, key) != 0) {
    return SDF_NUKI_RESULT_ERR_CRYPTO;
  }

  *plaintext_len = ciphertext_len - 16;
  if (*plaintext_len > plaintext_cap) {
    return SDF_NUKI_RESULT_ERR_TOO_LARGE;
  }

  memcpy(plaintext, m + 32, *plaintext_len);

  return SDF_NUKI_RESULT_OK;
}

static int sdf_nuki_build_pdata(uint32_t auth_id, uint16_t command,
                                const uint8_t *payload, size_t payload_len,
                                uint8_t *out, size_t out_cap, size_t *out_len) {
  size_t total_len = SDF_NUKI_PDATA_HEADER_LEN + payload_len + 2;

  if (total_len > out_cap) {
    return SDF_NUKI_RESULT_ERR_TOO_LARGE;
  }

  sdf_nuki_le32_write(out, auth_id);
  sdf_nuki_le16_write(out + 4, command);

  if (payload_len > 0 && payload != NULL) {
    memcpy(out + SDF_NUKI_PDATA_HEADER_LEN, payload, payload_len);
  }

  uint16_t crc =
      sdf_nuki_crc_ccitt(out, SDF_NUKI_PDATA_HEADER_LEN + payload_len);
  sdf_nuki_le16_write(out + SDF_NUKI_PDATA_HEADER_LEN + payload_len, crc);

  *out_len = total_len;

  return SDF_NUKI_RESULT_OK;
}

static void sdf_nuki_next_nonce(sdf_nuki_client_t *client,
                                uint8_t nonce[SDF_NUKI_NONCE_LEN]) {
  if (client == NULL || nonce == NULL) {
    return;
  }

  if (client->tx_nonce_counter == 0) {
    sdf_nuki_random(client->tx_nonce_salt, sizeof(client->tx_nonce_salt));
  }

  client->tx_nonce_counter++;

  memcpy(nonce, client->tx_nonce_salt, sizeof(client->tx_nonce_salt));
  sdf_nuki_le64_write(nonce + 8, client->tx_nonce_counter);

  uint64_t random_tail = ((uint64_t)esp_random() << 32) | esp_random();
  sdf_nuki_le64_write(nonce + 16, random_tail);
}

static bool sdf_nuki_nonce_seen(const sdf_nuki_client_t *client,
                                uint32_t authorization_id,
                                const uint8_t nonce[SDF_NUKI_NONCE_LEN]) {
  if (client == NULL || nonce == NULL) {
    return false;
  }

  for (size_t i = 0; i < client->rx_nonce_cache_count; ++i) {
    if (client->rx_nonce_auth_cache[i] == authorization_id &&
        memcmp(client->rx_nonce_cache[i], nonce, SDF_NUKI_NONCE_LEN) == 0) {
      return true;
    }
  }
  return false;
}

static void sdf_nuki_nonce_remember(sdf_nuki_client_t *client,
                                    uint32_t authorization_id,
                                    const uint8_t nonce[SDF_NUKI_NONCE_LEN]) {
  if (client == NULL || nonce == NULL || SDF_NUKI_NONCE_REPLAY_WINDOW == 0) {
    return;
  }

  size_t idx = client->rx_nonce_cache_write_idx;
  if (idx >= SDF_NUKI_NONCE_REPLAY_WINDOW) {
    idx = 0;
  }

  memcpy(client->rx_nonce_cache[idx], nonce, SDF_NUKI_NONCE_LEN);
  client->rx_nonce_auth_cache[idx] = authorization_id;
  client->rx_nonce_cache_write_idx =
      (uint8_t)((idx + 1) % SDF_NUKI_NONCE_REPLAY_WINDOW);
  if (client->rx_nonce_cache_count < SDF_NUKI_NONCE_REPLAY_WINDOW) {
    client->rx_nonce_cache_count++;
  }
}

static int sdf_nuki_build_encrypted_message_custom(
    sdf_nuki_client_t *client, const uint8_t nonce[SDF_NUKI_NONCE_LEN],
    uint32_t authorization_id,
    const uint8_t shared_key[SDF_NUKI_SHARED_KEY_LEN], uint16_t command,
    const uint8_t *payload, size_t payload_len, uint8_t *out, size_t out_cap,
    size_t *out_len) {
  if (client == NULL || nonce == NULL || shared_key == NULL || out == NULL ||
      out_len == NULL) {
    return SDF_NUKI_RESULT_ERR_ARG;
  }

  /* Use client->pd_buf as scratch for plaintext data assembly */
  size_t pdata_len = 0;

  int res =
      sdf_nuki_build_pdata(authorization_id, command, payload, payload_len,
                           client->pd_buf, sizeof(client->pd_buf), &pdata_len);
  if (res != SDF_NUKI_RESULT_OK) {
    return res;
  }

  /* Encrypt directly into the output buffer past the adata header */
  size_t ciphertext_cap =
      out_cap > SDF_NUKI_ADATA_LEN ? out_cap - SDF_NUKI_ADATA_LEN : 0;
  size_t ciphertext_len = 0;

  res = sdf_nuki_secretbox_encrypt(client->pd_buf, pdata_len, nonce, shared_key,
                                   out + SDF_NUKI_ADATA_LEN, ciphertext_cap,
                                   &ciphertext_len);
  if (res != SDF_NUKI_RESULT_OK) {
    return res;
  }

  size_t total_len = SDF_NUKI_ADATA_LEN + ciphertext_len;
  if (total_len > out_cap) {
    return SDF_NUKI_RESULT_ERR_TOO_LARGE;
  }

  memcpy(out, nonce, SDF_NUKI_NONCE_LEN);
  sdf_nuki_le32_write(out + SDF_NUKI_NONCE_LEN, authorization_id);
  sdf_nuki_le16_write(out + SDF_NUKI_NONCE_LEN + 4, (uint16_t)ciphertext_len);

  *out_len = total_len;

  return SDF_NUKI_RESULT_OK;
}

static int sdf_nuki_build_encrypted_message(sdf_nuki_client_t *client,
                                            uint16_t command,
                                            const uint8_t *payload,
                                            size_t payload_len, uint8_t *out,
                                            size_t out_cap, size_t *out_len) {
  if (client == NULL) {
    return SDF_NUKI_RESULT_ERR_ARG;
  }

  uint8_t nonce[SDF_NUKI_NONCE_LEN];
  sdf_nuki_next_nonce(client, nonce);

  return sdf_nuki_build_encrypted_message_custom(
      client, nonce, client->creds.authorization_id, client->creds.shared_key,
      command, payload, payload_len, out, out_cap, out_len);
}

static int sdf_nuki_process_encrypted_custom(
    sdf_nuki_client_t *client, uint32_t authorization_id,
    const uint8_t shared_key[SDF_NUKI_SHARED_KEY_LEN],
    sdf_nuki_message_cb message_cb, void *message_ctx) {
  if (client->rx_len < SDF_NUKI_ADATA_LEN) {
    return SDF_NUKI_RESULT_ERR_INCOMPLETE;
  }

  const uint8_t *buf = client->rx_buf;
  uint16_t msg_len = sdf_nuki_le16_read(buf + SDF_NUKI_NONCE_LEN + 4);

  size_t total_len = SDF_NUKI_ADATA_LEN + msg_len;
  if (msg_len < SDF_NUKI_SECRETBOX_OVERHEAD || total_len > client->rx_len) {
    return SDF_NUKI_RESULT_ERR_INCOMPLETE;
  }

  uint32_t adata_auth = sdf_nuki_le32_read(buf + SDF_NUKI_NONCE_LEN);
  if (adata_auth != authorization_id) {
    return SDF_NUKI_RESULT_ERR_AUTH;
  }

  if (sdf_nuki_nonce_seen(client, authorization_id, buf)) {
    return SDF_NUKI_RESULT_ERR_NONCE_REUSE;
  }

  size_t plaintext_len = 0;
  int res = sdf_nuki_secretbox_decrypt(buf + SDF_NUKI_ADATA_LEN, msg_len, buf,
                                       shared_key, client->pd_buf,
                                       sizeof(client->pd_buf), &plaintext_len);
  if (res != SDF_NUKI_RESULT_OK) {
    return res;
  }

  if (plaintext_len < SDF_NUKI_PDATA_HEADER_LEN + 2) {
    return SDF_NUKI_RESULT_ERR_ARG;
  }

  uint32_t pd_auth = sdf_nuki_le32_read(client->pd_buf);
  if (pd_auth != authorization_id) {
    return SDF_NUKI_RESULT_ERR_AUTH;
  }

  uint16_t crc_expected =
      sdf_nuki_le16_read(client->pd_buf + plaintext_len - 2);
  uint16_t crc_actual = sdf_nuki_crc_ccitt(client->pd_buf, plaintext_len - 2);
  if (crc_expected != crc_actual) {
    return SDF_NUKI_RESULT_ERR_CRC;
  }

  sdf_nuki_nonce_remember(client, authorization_id, buf);

  sdf_nuki_message_t msg = {
      .command_id = sdf_nuki_le16_read(client->pd_buf + 4),
      .payload = client->pd_buf + SDF_NUKI_PDATA_HEADER_LEN,
      .payload_len = plaintext_len - SDF_NUKI_PDATA_HEADER_LEN - 2};

  if (message_cb != NULL) {
    message_cb(message_ctx, &msg);
  }

  return SDF_NUKI_RESULT_OK;
}

static int sdf_nuki_process_encrypted(sdf_nuki_client_t *client) {
  return sdf_nuki_process_encrypted_custom(
      client, client->creds.authorization_id, client->creds.shared_key,
      client->message_cb, client->message_ctx);
}

void sdf_protocol_ble_init(void) {}

int sdf_nuki_client_init(sdf_nuki_client_t *client,
                         const sdf_nuki_credentials_t *creds,
                         sdf_nuki_send_cb send_encrypted_cb,
                         void *send_encrypted_ctx,
                         sdf_nuki_send_cb send_unencrypted_cb,
                         void *send_unencrypted_ctx,
                         sdf_nuki_message_cb message_cb, void *message_ctx) {
  if (client == NULL || creds == NULL || send_encrypted_cb == NULL) {
    return SDF_NUKI_RESULT_ERR_ARG;
  }

  memset(client, 0, sizeof(*client));
  memcpy(&client->creds, creds, sizeof(client->creds));
  client->send_encrypted_cb = send_encrypted_cb;
  client->send_encrypted_ctx = send_encrypted_ctx;
  client->send_unencrypted_cb = send_unencrypted_cb;
  client->send_unencrypted_ctx = send_unencrypted_ctx;
  client->message_cb = message_cb;
  client->message_ctx = message_ctx;

  return SDF_NUKI_RESULT_OK;
}

void sdf_nuki_client_reset_rx(sdf_nuki_client_t *client) {
  if (client == NULL) {
    return;
  }

  client->rx_len = 0;
  client->rx_expected = 0;
}

int sdf_nuki_client_send_unencrypted(sdf_nuki_client_t *client,
                                     uint16_t command, const uint8_t *payload,
                                     size_t payload_len) {
  if (client == NULL || client->send_encrypted_cb == NULL) {
    return SDF_NUKI_RESULT_ERR_ARG;
  }

  size_t message_len = 0;

  int res = sdf_nuki_build_unencrypted_message(
      command, payload, payload_len, client->tx_buf, sizeof(client->tx_buf),
      &message_len);
  if (res != SDF_NUKI_RESULT_OK) {
    return res;
  }

  if (client->send_unencrypted_cb != NULL) {
    return client->send_unencrypted_cb(client->send_unencrypted_ctx,
                                       client->tx_buf, message_len);
  }

  return client->send_encrypted_cb(client->send_encrypted_ctx, client->tx_buf,
                                   message_len);
}

int sdf_nuki_client_feed_encrypted(sdf_nuki_client_t *client,
                                   const uint8_t *data, size_t len) {
  if (client == NULL || data == NULL || len == 0) {
    return SDF_NUKI_RESULT_ERR_ARG;
  }

  if (client->rx_len + len > sizeof(client->rx_buf)) {
    sdf_nuki_client_reset_rx(client);
    return SDF_NUKI_RESULT_ERR_TOO_LARGE;
  }

  memcpy(client->rx_buf + client->rx_len, data, len);
  client->rx_len += len;

  for (;;) {
    if (client->rx_expected == 0) {
      if (client->rx_len < SDF_NUKI_ADATA_LEN) {
        return SDF_NUKI_RESULT_ERR_INCOMPLETE;
      }
      uint16_t msg_len =
          sdf_nuki_le16_read(client->rx_buf + SDF_NUKI_NONCE_LEN + 4);
      size_t total_len = SDF_NUKI_ADATA_LEN + msg_len;
      if (msg_len < SDF_NUKI_SECRETBOX_OVERHEAD ||
          total_len > sizeof(client->rx_buf)) {
        sdf_nuki_client_reset_rx(client);
        return SDF_NUKI_RESULT_ERR_TOO_LARGE;
      }
      client->rx_expected = total_len;
    }

    if (client->rx_len < client->rx_expected) {
      return SDF_NUKI_RESULT_ERR_INCOMPLETE;
    }

    int res = sdf_nuki_process_encrypted(client);
    if (res != SDF_NUKI_RESULT_OK) {
      sdf_nuki_client_reset_rx(client);
      return res;
    }

    size_t remaining = client->rx_len - client->rx_expected;
    if (remaining > 0) {
      memmove(client->rx_buf, client->rx_buf + client->rx_expected, remaining);
    }
    client->rx_len = remaining;
    client->rx_expected = 0;

    if (client->rx_len == 0) {
      break;
    }
  }

  return SDF_NUKI_RESULT_OK;
}

int sdf_nuki_client_feed_encrypted_custom(
    sdf_nuki_client_t *client, uint32_t authorization_id,
    const uint8_t shared_key[SDF_NUKI_SHARED_KEY_LEN], const uint8_t *data,
    size_t len, sdf_nuki_message_cb message_cb, void *message_ctx) {
  if (client == NULL || shared_key == NULL || data == NULL || len == 0) {
    return SDF_NUKI_RESULT_ERR_ARG;
  }

  if (client->rx_len + len > sizeof(client->rx_buf)) {
    sdf_nuki_client_reset_rx(client);
    return SDF_NUKI_RESULT_ERR_TOO_LARGE;
  }

  memcpy(client->rx_buf + client->rx_len, data, len);
  client->rx_len += len;

  for (;;) {
    if (client->rx_expected == 0) {
      if (client->rx_len < SDF_NUKI_ADATA_LEN) {
        return SDF_NUKI_RESULT_ERR_INCOMPLETE;
      }
      uint16_t msg_len =
          sdf_nuki_le16_read(client->rx_buf + SDF_NUKI_NONCE_LEN + 4);
      size_t total_len = SDF_NUKI_ADATA_LEN + msg_len;
      if (msg_len < SDF_NUKI_SECRETBOX_OVERHEAD ||
          total_len > sizeof(client->rx_buf)) {
        sdf_nuki_client_reset_rx(client);
        return SDF_NUKI_RESULT_ERR_TOO_LARGE;
      }
      client->rx_expected = total_len;
    }

    if (client->rx_len < client->rx_expected) {
      return SDF_NUKI_RESULT_ERR_INCOMPLETE;
    }

    int res = sdf_nuki_process_encrypted_custom(
        client, authorization_id, shared_key, message_cb, message_ctx);
    if (res != SDF_NUKI_RESULT_OK) {
      sdf_nuki_client_reset_rx(client);
      return res;
    }

    size_t remaining = client->rx_len - client->rx_expected;
    if (remaining > 0) {
      memmove(client->rx_buf, client->rx_buf + client->rx_expected, remaining);
    }
    client->rx_len = remaining;
    client->rx_expected = 0;

    if (client->rx_len == 0) {
      break;
    }
  }

  return SDF_NUKI_RESULT_OK;
}

int sdf_nuki_client_send_encrypted_custom(
    sdf_nuki_client_t *client, uint32_t authorization_id,
    const uint8_t shared_key[SDF_NUKI_SHARED_KEY_LEN], uint16_t command,
    const uint8_t *payload, size_t payload_len) {
  if (client == NULL || shared_key == NULL) {
    return SDF_NUKI_RESULT_ERR_ARG;
  }

  uint8_t nonce[SDF_NUKI_NONCE_LEN];
  sdf_nuki_next_nonce(client, nonce);

  size_t message_len = 0;

  int res = sdf_nuki_build_encrypted_message_custom(
      client, nonce, authorization_id, shared_key, command, payload,
      payload_len, client->tx_buf, sizeof(client->tx_buf), &message_len);
  if (res != SDF_NUKI_RESULT_OK) {
    return res;
  }

  return client->send_encrypted_cb(client->send_encrypted_ctx, client->tx_buf,
                                   message_len);
}

int sdf_nuki_client_send_request_data(sdf_nuki_client_t *client,
                                      uint16_t requested_command,
                                      const uint8_t *additional_data,
                                      size_t additional_len) {
  if (client == NULL) {
    return SDF_NUKI_RESULT_ERR_ARG;
  }

  uint8_t payload[2 + SDF_NUKI_MAX_PDATA];
  if (additional_len > SDF_NUKI_MAX_PDATA - 2) {
    return SDF_NUKI_RESULT_ERR_TOO_LARGE;
  }

  sdf_nuki_le16_write(payload, requested_command);
  if (additional_len > 0 && additional_data != NULL) {
    memcpy(payload + 2, additional_data, additional_len);
  }

  size_t message_len = 0;

  int res = sdf_nuki_build_encrypted_message(
      client, SDF_NUKI_CMD_REQUEST_DATA, payload, additional_len + 2,
      client->tx_buf, sizeof(client->tx_buf), &message_len);
  if (res != SDF_NUKI_RESULT_OK) {
    return res;
  }

  return client->send_encrypted_cb(client->send_encrypted_ctx, client->tx_buf,
                                   message_len);
}

int sdf_nuki_client_send_lock_action(
    sdf_nuki_client_t *client, uint8_t lock_action, uint8_t flags,
    const uint8_t *name_suffix, size_t name_suffix_len,
    const uint8_t nonce_nk[SDF_NUKI_CHALLENGE_NONCE_LEN]) {
  if (client == NULL || nonce_nk == NULL) {
    return SDF_NUKI_RESULT_ERR_ARG;
  }

  if (name_suffix_len > SDF_NUKI_NAME_SUFFIX_MAX) {
    return SDF_NUKI_RESULT_ERR_ARG;
  }

  uint8_t payload[1 + 4 + 1 + SDF_NUKI_NAME_SUFFIX_MAX +
                  SDF_NUKI_CHALLENGE_NONCE_LEN];
  size_t offset = 0;

  payload[offset++] = lock_action;
  sdf_nuki_le32_write(payload + offset, client->creds.app_id);
  offset += 4;
  payload[offset++] = flags;

  if (name_suffix_len > 0) {
    memset(payload + offset, 0, SDF_NUKI_NAME_SUFFIX_MAX);
    memcpy(payload + offset, name_suffix, name_suffix_len);
    offset += SDF_NUKI_NAME_SUFFIX_MAX;
  }

  memcpy(payload + offset, nonce_nk, SDF_NUKI_CHALLENGE_NONCE_LEN);
  offset += SDF_NUKI_CHALLENGE_NONCE_LEN;

  size_t message_len = 0;

  int res = sdf_nuki_build_encrypted_message(
      client, SDF_NUKI_CMD_LOCK_ACTION, payload, offset, client->tx_buf,
      sizeof(client->tx_buf), &message_len);
  if (res != SDF_NUKI_RESULT_OK) {
    return res;
  }

  return client->send_encrypted_cb(client->send_encrypted_ctx, client->tx_buf,
                                   message_len);
}

int sdf_nuki_client_send_simple_lock_action(
    sdf_nuki_client_t *client, uint8_t lock_action, const uint8_t *name_suffix,
    size_t name_suffix_len,
    const uint8_t nonce_nk[SDF_NUKI_CHALLENGE_NONCE_LEN]) {
  if (client == NULL || nonce_nk == NULL) {
    return SDF_NUKI_RESULT_ERR_ARG;
  }

  if (name_suffix_len > SDF_NUKI_NAME_SUFFIX_MAX) {
    return SDF_NUKI_RESULT_ERR_ARG;
  }

  uint8_t payload[1 + SDF_NUKI_NAME_SUFFIX_MAX + SDF_NUKI_CHALLENGE_NONCE_LEN];
  size_t offset = 0;

  payload[offset++] = lock_action;

  if (name_suffix_len > 0) {
    memset(payload + offset, 0, SDF_NUKI_NAME_SUFFIX_MAX);
    memcpy(payload + offset, name_suffix, name_suffix_len);
    offset += SDF_NUKI_NAME_SUFFIX_MAX;
  }

  memcpy(payload + offset, nonce_nk, SDF_NUKI_CHALLENGE_NONCE_LEN);
  offset += SDF_NUKI_CHALLENGE_NONCE_LEN;

  size_t message_len = 0;

  int res = sdf_nuki_build_encrypted_message(
      client, SDF_NUKI_CMD_SIMPLE_LOCK_ACTION, payload, offset, client->tx_buf,
      sizeof(client->tx_buf), &message_len);
  if (res != SDF_NUKI_RESULT_OK) {
    return res;
  }

  return client->send_encrypted_cb(client->send_encrypted_ctx, client->tx_buf,
                                   message_len);
}

int sdf_nuki_compute_shared_key(
    const uint8_t private_key[SDF_NUKI_SHARED_KEY_LEN],
    const uint8_t public_key[SDF_NUKI_SHARED_KEY_LEN],
    uint8_t shared_key_out[SDF_NUKI_SHARED_KEY_LEN]) {
  if (private_key == NULL || public_key == NULL || shared_key_out == NULL) {
    return SDF_NUKI_RESULT_ERR_ARG;
  }

  uint8_t dh_key[SDF_NUKI_SHARED_KEY_LEN];
  if (crypto_scalarmult(dh_key, private_key, public_key) != 0) {
    return SDF_NUKI_RESULT_ERR_CRYPTO;
  }

  static const unsigned char sigma[16] = "expand 32-byte k";
  static const unsigned char zeros[16] = {0};

  if (crypto_core_hsalsa20(shared_key_out, zeros, dh_key, sigma) != 0) {
    return SDF_NUKI_RESULT_ERR_CRYPTO;
  }

  return SDF_NUKI_RESULT_OK;
}

int sdf_nuki_compute_authenticator(const uint8_t *data, size_t data_len,
                                   const uint8_t key[SDF_NUKI_SHARED_KEY_LEN],
                                   uint8_t out[32]) {
  if (data == NULL || key == NULL || out == NULL) {
    return SDF_NUKI_RESULT_ERR_ARG;
  }

  const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (info == NULL) {
    return SDF_NUKI_RESULT_ERR_CRYPTO;
  }

  if (mbedtls_md_hmac(info, key, SDF_NUKI_SHARED_KEY_LEN, data, data_len,
                      out) != 0) {
    return SDF_NUKI_RESULT_ERR_CRYPTO;
  }

  return SDF_NUKI_RESULT_OK;
}

int sdf_nuki_parse_challenge(const sdf_nuki_message_t *msg,
                             uint8_t nonce_nk[SDF_NUKI_CHALLENGE_NONCE_LEN]) {
  if (msg == NULL || nonce_nk == NULL) {
    return SDF_NUKI_RESULT_ERR_ARG;
  }

  if (msg->command_id != SDF_NUKI_CMD_CHALLENGE ||
      msg->payload_len < SDF_NUKI_CHALLENGE_NONCE_LEN) {
    return SDF_NUKI_RESULT_ERR_ARG;
  }

  memcpy(nonce_nk, msg->payload, SDF_NUKI_CHALLENGE_NONCE_LEN);
  return SDF_NUKI_RESULT_OK;
}

int sdf_nuki_parse_status(const sdf_nuki_message_t *msg, uint8_t *status_out) {
  if (msg == NULL || status_out == NULL) {
    return SDF_NUKI_RESULT_ERR_ARG;
  }

  if (msg->command_id != SDF_NUKI_CMD_STATUS || msg->payload_len < 1) {
    return SDF_NUKI_RESULT_ERR_ARG;
  }

  *status_out = msg->payload[0];
  return SDF_NUKI_RESULT_OK;
}

int sdf_nuki_parse_keyturner_states(const sdf_nuki_message_t *msg,
                                    sdf_keyturner_state_t *state_out) {
  if (msg == NULL || state_out == NULL) {
    return SDF_NUKI_RESULT_ERR_ARG;
  }

  if (msg->command_id != SDF_NUKI_CMD_KEYTURNER_STATES ||
      msg->payload_len < 15) {
    return SDF_NUKI_RESULT_ERR_ARG;
  }

  memset(state_out, 0, sizeof(*state_out));

  const uint8_t *p = msg->payload;
  state_out->nuki_state = p[0];
  state_out->lock_state = p[1];
  state_out->trigger = p[2];
  state_out->current_time_year = sdf_nuki_le16_read(p + 3);
  state_out->current_time_month = p[5];
  state_out->current_time_day = p[6];
  state_out->current_time_hour = p[7];
  state_out->current_time_minute = p[8];
  state_out->current_time_second = p[9];
  state_out->timezone_offset_minutes = (int16_t)sdf_nuki_le16_read(p + 10);
  state_out->critical_battery_state = p[12];
  state_out->lock_n_go_timer = p[13];
  state_out->last_lock_action = p[14];

  // Optional fields are parsed progressively for extended payload variants.
  if (msg->payload_len >= 16) {
    state_out->has_last_lock_action_trigger = true;
    state_out->last_lock_action_trigger = p[15];
  }

  if (msg->payload_len >= 17) {
    state_out->has_last_lock_action_completion_status = true;
    state_out->last_lock_action_completion_status = p[16];
  }

  if (msg->payload_len >= 18) {
    state_out->has_door_sensor_state = true;
    state_out->door_sensor_state = p[17];
  }

  if (msg->payload_len >= 19) {
    state_out->has_nightmode_active = true;
    state_out->nightmode_active = p[18];
  }

  return SDF_NUKI_RESULT_OK;
}

int sdf_nuki_parse_error_report(const sdf_nuki_message_t *msg,
                                sdf_error_report_t *error_out) {
  if (msg == NULL || error_out == NULL) {
    return SDF_NUKI_RESULT_ERR_ARG;
  }

  if (msg->command_id != SDF_NUKI_CMD_ERROR_REPORT || msg->payload_len < 3) {
    return SDF_NUKI_RESULT_ERR_ARG;
  }

  error_out->error_code = (int8_t)msg->payload[0];
  error_out->command_identifier = sdf_nuki_le16_read(msg->payload + 1);

  return SDF_NUKI_RESULT_OK;
}

#endif /* !CONFIG_IDF_TARGET_LINUX */
