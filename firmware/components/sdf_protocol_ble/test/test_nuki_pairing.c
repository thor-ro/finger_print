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

static int mock_send_cb(void *ctx, const uint8_t *data, size_t len) {
  (void)ctx;
  s_send_call_count++;
  s_last_send_len = (int)len;
  return SDF_NUKI_RESULT_OK;
}

static void setup_client(sdf_nuki_client_t *client) {
  sdf_nuki_credentials_t creds;
  memset(&creds, 0, sizeof(creds));
  creds.authorization_id = 12345;
  creds.app_id = 1;
  memset(creds.shared_key, 0xAA, sizeof(creds.shared_key));

  sdf_nuki_client_init(client, &creds, mock_send_cb, NULL, mock_send_cb, NULL,
                       NULL, NULL);
  s_send_call_count = 0;
  s_last_send_len = 0;
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

  /* Feed data larger than rx_buf (96 bytes) */
  uint8_t large_data[100];
  memset(large_data, 0xFF, sizeof(large_data));

  int res = sdf_nuki_pairing_handle_unencrypted(&pairing, large_data,
                                                sizeof(large_data));
  TEST_ASSERT_EQUAL(SDF_NUKI_RESULT_ERR_TOO_LARGE, res);
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
