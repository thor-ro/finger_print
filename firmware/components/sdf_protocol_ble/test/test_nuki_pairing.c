/**
 * @file test_nuki_pairing.c
 * @brief Unit tests for sdf_nuki_pairing module.
 */
#include "sdf_nuki_pairing.h"
#include "unity.h"
#include <string.h>

/* ---------- Helpers ---------- */

static int s_send_call_count;
static int s_last_send_len;
static int s_last_send_kind;
static uint8_t s_last_send_buf[SDF_NUKI_MAX_MESSAGE];

enum {
  SEND_KIND_NONE = 0,
  SEND_KIND_ENCRYPTED,
  SEND_KIND_UNENCRYPTED,
};

static uint16_t test_le16_read(const uint8_t *src) {
  return (uint16_t)src[0] | ((uint16_t)src[1] << 8);
}

static void test_le16_write(uint8_t *dst, uint16_t value) {
  dst[0] = (uint8_t)(value & 0xFF);
  dst[1] = (uint8_t)((value >> 8) & 0xFF);
}

static uint16_t test_crc_ccitt(const uint8_t *data, size_t len) {
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

static size_t build_unencrypted_message(uint16_t command, const uint8_t *payload,
                                        size_t payload_len, uint8_t *out) {
  test_le16_write(out, command);
  if (payload_len > 0) {
    memcpy(out + 2, payload, payload_len);
  }

  uint16_t crc = test_crc_ccitt(out, 2 + payload_len);
  test_le16_write(out + 2 + payload_len, crc);
  return 2 + payload_len + 2;
}

static int mock_send_common(int kind, const uint8_t *data, size_t len) {
  TEST_ASSERT_TRUE(len <= sizeof(s_last_send_buf));
  memcpy(s_last_send_buf, data, len);
  s_last_send_kind = kind;
  s_send_call_count++;
  s_last_send_len = (int)len;
  return SDF_NUKI_RESULT_OK;
}

static int mock_send_encrypted_cb(void *ctx, const uint8_t *data, size_t len) {
  (void)ctx;
  return mock_send_common(SEND_KIND_ENCRYPTED, data, len);
}

static int mock_send_unencrypted_cb(void *ctx, const uint8_t *data, size_t len) {
  (void)ctx;
  return mock_send_common(SEND_KIND_UNENCRYPTED, data, len);
}

static void setup_client(sdf_nuki_client_t *client) {
  sdf_nuki_credentials_t creds;
  memset(&creds, 0, sizeof(creds));
  creds.authorization_id = 12345;
  creds.app_id = 1;
  memset(creds.shared_key, 0xAA, sizeof(creds.shared_key));

  sdf_nuki_client_init(client, &creds, mock_send_encrypted_cb, NULL,
                       mock_send_unencrypted_cb, NULL, NULL, NULL);
  s_send_call_count = 0;
  s_last_send_len = 0;
  s_last_send_kind = SEND_KIND_NONE;
  memset(s_last_send_buf, 0, sizeof(s_last_send_buf));
}

/* ---------- sdf_nuki_pairing_init ---------- */

void test_pairing_init_success(void) {
  sdf_nuki_client_t client;
  setup_client(&client);

  sdf_nuki_pairing_t pairing;
  int res = sdf_nuki_pairing_init(&pairing, &client, 0, 42, "TestApp");

  /* crypto_scalarmult (Curve25519) may not be supported on Linux mbedTLS */
  if (res == SDF_NUKI_RESULT_ERR_CRYPTO) {
    TEST_IGNORE_MESSAGE("Curve25519 not supported on this host mbedTLS build");
    return;
  }

  TEST_ASSERT_EQUAL(SDF_NUKI_RESULT_OK, res);
  TEST_ASSERT_EQUAL(SDF_NUKI_PAIRING_IDLE, pairing.state);
  TEST_ASSERT_EQUAL(0, pairing.id_type);
  TEST_ASSERT_EQUAL(42, pairing.app_id);
  TEST_ASSERT_EQUAL_STRING_LEN("TestApp", (char *)pairing.name, 7);

  /* Keypair should have been generated (public key should not be all zeros) */
  uint8_t zeros[32] = {0};
  TEST_ASSERT_FALSE(memcmp(pairing.public_key, zeros, 32) == 0);
}

void test_pairing_init_null_args(void) {
  sdf_nuki_client_t client;
  setup_client(&client);
  sdf_nuki_pairing_t pairing;

  TEST_ASSERT_EQUAL(SDF_NUKI_RESULT_ERR_ARG,
                    sdf_nuki_pairing_init(NULL, &client, 0, 1, "x"));
  TEST_ASSERT_EQUAL(SDF_NUKI_RESULT_ERR_ARG,
                    sdf_nuki_pairing_init(&pairing, NULL, 0, 1, "x"));
  TEST_ASSERT_EQUAL(SDF_NUKI_RESULT_ERR_ARG,
                    sdf_nuki_pairing_init(&pairing, &client, 0, 1, NULL));
}

void test_pairing_init_long_name_truncated(void) {
  sdf_nuki_client_t client;
  setup_client(&client);
  sdf_nuki_pairing_t pairing;

  /* Name longer than 32 bytes should be truncated */
  const char *long_name = "ThisIsAVeryLongNameThatExceedsThirtyTwoCharacters";
  int res = sdf_nuki_pairing_init(&pairing, &client, 0, 1, long_name);

  /* crypto_scalarmult (Curve25519) may not be supported on Linux mbedTLS */
  if (res == SDF_NUKI_RESULT_ERR_CRYPTO) {
    TEST_IGNORE_MESSAGE("Curve25519 not supported on this host mbedTLS build");
    return;
  }

  TEST_ASSERT_EQUAL(SDF_NUKI_RESULT_OK, res);
  TEST_ASSERT_EQUAL_STRING_LEN(long_name, (char *)pairing.name, 32);
}

/* ---------- sdf_nuki_pairing_start ---------- */

void test_pairing_start_sends_request(void) {
  sdf_nuki_client_t client;
  setup_client(&client);
  sdf_nuki_pairing_t pairing;
  sdf_nuki_pairing_init(&pairing, &client, 0, 1, "Test");

  int res = sdf_nuki_pairing_start(&pairing);
  TEST_ASSERT_EQUAL(SDF_NUKI_RESULT_OK, res);
  TEST_ASSERT_EQUAL(SDF_NUKI_PAIRING_WAIT_PUBLIC_KEY, pairing.state);
  TEST_ASSERT_EQUAL(1, s_send_call_count);
}

void test_pairing_start_null_args(void) {
  TEST_ASSERT_EQUAL(SDF_NUKI_RESULT_ERR_ARG, sdf_nuki_pairing_start(NULL));

  sdf_nuki_pairing_t pairing;
  memset(&pairing, 0, sizeof(pairing));
  pairing.client = NULL;
  TEST_ASSERT_EQUAL(SDF_NUKI_RESULT_ERR_ARG, sdf_nuki_pairing_start(&pairing));
}

/* ---------- sdf_nuki_pairing_get_credentials ---------- */

void test_pairing_get_credentials_not_complete(void) {
  sdf_nuki_client_t client;
  setup_client(&client);
  sdf_nuki_pairing_t pairing;
  sdf_nuki_pairing_init(&pairing, &client, 0, 1, "Test");

  sdf_nuki_credentials_t creds;
  TEST_ASSERT_EQUAL(SDF_NUKI_RESULT_ERR_INCOMPLETE,
                    sdf_nuki_pairing_get_credentials(&pairing, &creds));
}

void test_pairing_get_credentials_null_args(void) {
  sdf_nuki_pairing_t pairing;
  memset(&pairing, 0, sizeof(pairing));
  sdf_nuki_credentials_t creds;

  TEST_ASSERT_EQUAL(SDF_NUKI_RESULT_ERR_ARG,
                    sdf_nuki_pairing_get_credentials(NULL, &creds));
  TEST_ASSERT_EQUAL(SDF_NUKI_RESULT_ERR_ARG,
                    sdf_nuki_pairing_get_credentials(&pairing, NULL));
}

void test_pairing_get_credentials_when_complete(void) {
  /* Manually set state to COMPLETE to verify credential extraction */
  sdf_nuki_client_t client;
  setup_client(&client);
  sdf_nuki_pairing_t pairing;
  sdf_nuki_pairing_init(&pairing, &client, 0, 99, "Test");

  pairing.state = SDF_NUKI_PAIRING_COMPLETE;
  pairing.authorization_id = 0xDEADBEEF;
  memset(pairing.shared_key, 0x77, sizeof(pairing.shared_key));

  sdf_nuki_credentials_t creds;
  TEST_ASSERT_EQUAL(SDF_NUKI_RESULT_OK,
                    sdf_nuki_pairing_get_credentials(&pairing, &creds));
  TEST_ASSERT_EQUAL_UINT32(0xDEADBEEF, creds.authorization_id);
  TEST_ASSERT_EQUAL_UINT32(99, creds.app_id);

  uint8_t expected_key[32];
  memset(expected_key, 0x77, sizeof(expected_key));
  TEST_ASSERT_EQUAL_MEMORY(expected_key, creds.shared_key, 32);
}

/* ---------- sdf_nuki_pairing_handle_unencrypted ---------- */

void test_pairing_handle_unencrypted_null_args(void) {
  uint8_t data[4] = {0};
  sdf_nuki_pairing_t pairing;
  memset(&pairing, 0, sizeof(pairing));

  TEST_ASSERT_EQUAL(
      SDF_NUKI_RESULT_ERR_ARG,
      sdf_nuki_pairing_handle_unencrypted(NULL, data, sizeof(data)));
  TEST_ASSERT_EQUAL(SDF_NUKI_RESULT_ERR_ARG,
                    sdf_nuki_pairing_handle_unencrypted(&pairing, NULL, 4));
  TEST_ASSERT_EQUAL(SDF_NUKI_RESULT_ERR_ARG,
                    sdf_nuki_pairing_handle_unencrypted(&pairing, data, 0));
}

void test_pairing_handle_unencrypted_overflow_protection(void) {
  sdf_nuki_client_t client;
  setup_client(&client);
  sdf_nuki_pairing_t pairing;
  sdf_nuki_pairing_init(&pairing, &client, 0, 1, "Test");

  uint8_t large_data[SDF_NUKI_MAX_MESSAGE + 1];
  memset(large_data, 0xFF, sizeof(large_data));

  int res = sdf_nuki_pairing_handle_unencrypted(&pairing, large_data,
                                                sizeof(large_data));
  TEST_ASSERT_EQUAL(SDF_NUKI_RESULT_ERR_TOO_LARGE, res);
}

void test_pairing_first_challenge_sends_only_authenticator(void) {
  sdf_nuki_client_t client;
  setup_client(&client);

  sdf_nuki_pairing_t pairing;
  memset(&pairing, 0, sizeof(pairing));
  pairing.client = &client;
  pairing.state = SDF_NUKI_PAIRING_WAIT_CHALLENGE;
  memset(pairing.public_key, 0x11, sizeof(pairing.public_key));
  memset(pairing.smartlock_public_key, 0x22, sizeof(pairing.smartlock_public_key));
  memset(pairing.shared_key, 0x33, sizeof(pairing.shared_key));

  uint8_t nonce_nk[32];
  memset(nonce_nk, 0x44, sizeof(nonce_nk));

  uint8_t frame[36];
  size_t frame_len = build_unencrypted_message(SDF_NUKI_CMD_CHALLENGE, nonce_nk,
                                               sizeof(nonce_nk), frame);

  TEST_ASSERT_EQUAL(SDF_NUKI_RESULT_OK,
                    sdf_nuki_pairing_handle_unencrypted(&pairing, frame, frame_len));
  TEST_ASSERT_EQUAL(SDF_NUKI_PAIRING_WAIT_AUTHORIZATION_DATA, pairing.state);
  TEST_ASSERT_EQUAL(1, s_send_call_count);
  TEST_ASSERT_EQUAL(SEND_KIND_UNENCRYPTED, s_last_send_kind);
  TEST_ASSERT_EQUAL(36, s_last_send_len);
  TEST_ASSERT_EQUAL_UINT16(SDF_NUKI_CMD_AUTHORIZATION_AUTHENTICATOR,
                           test_le16_read(s_last_send_buf));
  TEST_ASSERT_EQUAL_MEMORY(nonce_nk, pairing.nonce_nk, sizeof(nonce_nk));
}

void test_pairing_second_challenge_sends_authorization_data(void) {
  sdf_nuki_client_t client;
  setup_client(&client);

  sdf_nuki_pairing_t pairing;
  memset(&pairing, 0, sizeof(pairing));
  pairing.client = &client;
  pairing.state = SDF_NUKI_PAIRING_WAIT_AUTHORIZATION_DATA;
  pairing.id_type = 0;
  pairing.app_id = 42;
  memset(pairing.name, 0, sizeof(pairing.name));
  memcpy(pairing.name, "Test", 4);
  memset(pairing.shared_key, 0x55, sizeof(pairing.shared_key));

  uint8_t nonce_nk[32];
  memset(nonce_nk, 0x66, sizeof(nonce_nk));

  uint8_t frame[36];
  size_t frame_len = build_unencrypted_message(SDF_NUKI_CMD_CHALLENGE, nonce_nk,
                                               sizeof(nonce_nk), frame);

  TEST_ASSERT_EQUAL(SDF_NUKI_RESULT_OK,
                    sdf_nuki_pairing_handle_unencrypted(&pairing, frame, frame_len));
  TEST_ASSERT_EQUAL(SDF_NUKI_PAIRING_WAIT_AUTHORIZATION_ID, pairing.state);
  TEST_ASSERT_EQUAL(1, s_send_call_count);
  TEST_ASSERT_EQUAL(SEND_KIND_UNENCRYPTED, s_last_send_kind);
  TEST_ASSERT_EQUAL(105, s_last_send_len);
  TEST_ASSERT_EQUAL_UINT16(SDF_NUKI_CMD_AUTHORIZATION_DATA,
                           test_le16_read(s_last_send_buf));
  TEST_ASSERT_EQUAL_MEMORY(nonce_nk, pairing.nonce_nk, sizeof(nonce_nk));
}

void test_pairing_authorization_id_completes_pairing(void) {
  sdf_nuki_client_t client;
  setup_client(&client);

  sdf_nuki_pairing_t pairing;
  memset(&pairing, 0, sizeof(pairing));
  pairing.client = &client;
  pairing.state = SDF_NUKI_PAIRING_WAIT_AUTHORIZATION_ID;
  memset(pairing.shared_key, 0x77, sizeof(pairing.shared_key));
  memset(pairing.nonce_na, 0x33, sizeof(pairing.nonce_na));

  uint32_t auth_id = 0x12345678;
  uint8_t uuid[16];
  uint8_t returned_nonce[32];
  memset(uuid, 0x11, sizeof(uuid));
  memset(returned_nonce, 0x44, sizeof(returned_nonce));

  uint8_t auth_input[4 + 16 + 32 + 32];
  auth_input[0] = (uint8_t)(auth_id & 0xFF);
  auth_input[1] = (uint8_t)((auth_id >> 8) & 0xFF);
  auth_input[2] = (uint8_t)((auth_id >> 16) & 0xFF);
  auth_input[3] = (uint8_t)((auth_id >> 24) & 0xFF);
  memcpy(auth_input + 4, uuid, sizeof(uuid));
  memcpy(auth_input + 20, returned_nonce, sizeof(returned_nonce));
  memcpy(auth_input + 52, pairing.nonce_na, sizeof(pairing.nonce_na));

  uint8_t authenticator[32];
  TEST_ASSERT_EQUAL(SDF_NUKI_RESULT_OK,
                    sdf_nuki_compute_authenticator(auth_input, sizeof(auth_input),
                                                   pairing.shared_key, authenticator));

  uint8_t payload[32 + 4 + 16 + 32];
  memcpy(payload, authenticator, 32);
  payload[32] = (uint8_t)(auth_id & 0xFF);
  payload[33] = (uint8_t)((auth_id >> 8) & 0xFF);
  payload[34] = (uint8_t)((auth_id >> 16) & 0xFF);
  payload[35] = (uint8_t)((auth_id >> 24) & 0xFF);
  memcpy(payload + 36, uuid, sizeof(uuid));
  memcpy(payload + 52, returned_nonce, sizeof(returned_nonce));

  uint8_t frame[88];
  size_t frame_len = build_unencrypted_message(SDF_NUKI_CMD_AUTHORIZATION_ID,
                                               payload, sizeof(payload), frame);

  TEST_ASSERT_EQUAL(SDF_NUKI_RESULT_OK,
                    sdf_nuki_pairing_handle_unencrypted(&pairing, frame, frame_len));
  TEST_ASSERT_EQUAL(SDF_NUKI_PAIRING_COMPLETE, pairing.state);
  TEST_ASSERT_EQUAL(auth_id, pairing.authorization_id);
  TEST_ASSERT_EQUAL_MEMORY(returned_nonce, pairing.nonce_nk,
                           sizeof(returned_nonce));
  TEST_ASSERT_EQUAL(1, s_send_call_count);
  TEST_ASSERT_EQUAL(SEND_KIND_UNENCRYPTED, s_last_send_kind);
  TEST_ASSERT_EQUAL(40, s_last_send_len);
  TEST_ASSERT_EQUAL_UINT16(SDF_NUKI_CMD_AUTHORIZATION_ID_CONFIRMATION,
                           test_le16_read(s_last_send_buf));
}

void test_pairing_handle_encrypted_null_args(void) {
  uint8_t data[4] = {0};
  sdf_nuki_pairing_t pairing;
  memset(&pairing, 0, sizeof(pairing));

  TEST_ASSERT_EQUAL(SDF_NUKI_RESULT_ERR_ARG, sdf_nuki_pairing_handle_encrypted(
                                                 NULL, data, sizeof(data)));
  TEST_ASSERT_EQUAL(SDF_NUKI_RESULT_ERR_ARG,
                    sdf_nuki_pairing_handle_encrypted(&pairing, NULL, 4));
  TEST_ASSERT_EQUAL(SDF_NUKI_RESULT_ERR_ARG,
                    sdf_nuki_pairing_handle_encrypted(&pairing, data, 0));
}
