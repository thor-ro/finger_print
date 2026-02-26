/**
 * @file test_protocol_ble.c
 * @brief Unit tests for sdf_protocol_ble client and message parsing.
 */
#include "sdf_protocol_ble.h"
#include "unity.h"
#include <string.h>

/* ---------- Helpers ---------- */

static uint8_t s_sent_data[2048];
static size_t s_sent_len;
static int s_send_count;

static int mock_send_encrypted(void *ctx, const uint8_t *data, size_t len) {
  (void)ctx;
  if (len <= sizeof(s_sent_data)) {
    memcpy(s_sent_data, data, len);
  }
  s_sent_len = len;
  s_send_count++;
  return SDF_NUKI_RESULT_OK;
}

static int mock_send_unencrypted(void *ctx, const uint8_t *data, size_t len) {
  (void)ctx;
  if (len <= sizeof(s_sent_data)) {
    memcpy(s_sent_data, data, len);
  }
  s_sent_len = len;
  s_send_count++;
  return SDF_NUKI_RESULT_OK;
}

static sdf_nuki_message_t s_received_msg;
static int s_msg_count;

static void mock_message_cb(void *ctx, const sdf_nuki_message_t *msg) {
  (void)ctx;
  if (msg != NULL) {
    s_received_msg = *msg;
    s_msg_count++;
  }
}

static void reset_mocks(void) {
  memset(s_sent_data, 0, sizeof(s_sent_data));
  s_sent_len = 0;
  s_send_count = 0;
  memset(&s_received_msg, 0, sizeof(s_received_msg));
  s_msg_count = 0;
}

static void make_client(sdf_nuki_client_t *client, uint32_t auth_id) {
  sdf_nuki_credentials_t creds;
  memset(&creds, 0, sizeof(creds));
  creds.authorization_id = auth_id;
  creds.app_id = 1;
  memset(creds.shared_key, 0xBB, sizeof(creds.shared_key));

  sdf_nuki_client_init(client, &creds, mock_send_encrypted, NULL,
                       mock_send_unencrypted, NULL, mock_message_cb, NULL);
  reset_mocks();
}

/* ---------- sdf_nuki_client_init ---------- */

void test_client_init_success(void) {
  sdf_nuki_client_t client;
  sdf_nuki_credentials_t creds;
  memset(&creds, 0, sizeof(creds));
  creds.authorization_id = 42;
  memset(creds.shared_key, 0xCC, 32);

  int res =
      sdf_nuki_client_init(&client, &creds, mock_send_encrypted, NULL,
                           mock_send_unencrypted, NULL, mock_message_cb, NULL);
  TEST_ASSERT_EQUAL(SDF_NUKI_RESULT_OK, res);
  TEST_ASSERT_EQUAL_UINT32(42, client.creds.authorization_id);
}

void test_client_init_null_args(void) {
  sdf_nuki_client_t client;
  sdf_nuki_credentials_t creds;
  memset(&creds, 0, sizeof(creds));

  TEST_ASSERT_EQUAL(SDF_NUKI_RESULT_ERR_ARG,
                    sdf_nuki_client_init(NULL, &creds, mock_send_encrypted,
                                         NULL, NULL, NULL, NULL, NULL));
  TEST_ASSERT_EQUAL(SDF_NUKI_RESULT_ERR_ARG,
                    sdf_nuki_client_init(&client, NULL, mock_send_encrypted,
                                         NULL, NULL, NULL, NULL, NULL));
  TEST_ASSERT_EQUAL(SDF_NUKI_RESULT_ERR_ARG,
                    sdf_nuki_client_init(&client, &creds, NULL, NULL, NULL,
                                         NULL, NULL, NULL));
}

/* ---------- sdf_nuki_client_reset_rx ---------- */

void test_client_reset_rx(void) {
  sdf_nuki_client_t client;
  make_client(&client, 100);
  client.rx_len = 50;
  client.rx_expected = 100;

  sdf_nuki_client_reset_rx(&client);
  TEST_ASSERT_EQUAL(0, client.rx_len);
  TEST_ASSERT_EQUAL(0, client.rx_expected);
}

void test_client_reset_rx_null_safety(void) {
  /* Should not crash */
  sdf_nuki_client_reset_rx(NULL);
}

/* ---------- sdf_nuki_parse_challenge ---------- */

void test_parse_challenge_success(void) {
  uint8_t nonce_payload[32];
  memset(nonce_payload, 0xAA, sizeof(nonce_payload));

  sdf_nuki_message_t msg = {.command_id = SDF_NUKI_CMD_CHALLENGE,
                            .payload = nonce_payload,
                            .payload_len = 32};

  uint8_t nonce_out[32];
  TEST_ASSERT_EQUAL(SDF_NUKI_RESULT_OK,
                    sdf_nuki_parse_challenge(&msg, nonce_out));
  TEST_ASSERT_EQUAL_MEMORY(nonce_payload, nonce_out, 32);
}

void test_parse_challenge_wrong_command(void) {
  uint8_t payload[32] = {0};
  sdf_nuki_message_t msg = {
      .command_id = SDF_NUKI_CMD_STATUS, .payload = payload, .payload_len = 32};

  uint8_t nonce[32];
  TEST_ASSERT_EQUAL(SDF_NUKI_RESULT_ERR_ARG,
                    sdf_nuki_parse_challenge(&msg, nonce));
}

void test_parse_challenge_null_args(void) {
  uint8_t nonce[32];
  sdf_nuki_message_t msg = {.command_id = SDF_NUKI_CMD_CHALLENGE};

  TEST_ASSERT_EQUAL(SDF_NUKI_RESULT_ERR_ARG,
                    sdf_nuki_parse_challenge(NULL, nonce));
  TEST_ASSERT_EQUAL(SDF_NUKI_RESULT_ERR_ARG,
                    sdf_nuki_parse_challenge(&msg, NULL));
}

void test_parse_challenge_too_short(void) {
  uint8_t payload[16] = {0};
  sdf_nuki_message_t msg = {.command_id = SDF_NUKI_CMD_CHALLENGE,
                            .payload = payload,
                            .payload_len = 16};

  uint8_t nonce[32];
  TEST_ASSERT_EQUAL(SDF_NUKI_RESULT_ERR_ARG,
                    sdf_nuki_parse_challenge(&msg, nonce));
}

/* ---------- sdf_nuki_parse_status ---------- */

void test_parse_status_success(void) {
  uint8_t payload[] = {0x01};
  sdf_nuki_message_t msg = {
      .command_id = SDF_NUKI_CMD_STATUS, .payload = payload, .payload_len = 1};

  uint8_t status = 0xFF;
  TEST_ASSERT_EQUAL(SDF_NUKI_RESULT_OK, sdf_nuki_parse_status(&msg, &status));
  TEST_ASSERT_EQUAL_UINT8(0x01, status);
}

void test_parse_status_wrong_command(void) {
  uint8_t payload[] = {0x01};
  sdf_nuki_message_t msg = {.command_id = SDF_NUKI_CMD_CHALLENGE,
                            .payload = payload,
                            .payload_len = 1};

  uint8_t status;
  TEST_ASSERT_EQUAL(SDF_NUKI_RESULT_ERR_ARG,
                    sdf_nuki_parse_status(&msg, &status));
}

/* ---------- sdf_nuki_parse_keyturner_states ---------- */

void test_parse_keyturner_states_minimum(void) {
  /* Minimum payload: 15 bytes */
  uint8_t payload[15];
  memset(payload, 0, sizeof(payload));
  payload[0] = 0x02; /* nuki_state */
  payload[1] = 0x03; /* lock_state */
  payload[2] = 0x01; /* trigger */
  /* year LE at [3..4] */
  payload[3] = 0xE6;
  payload[4] = 0x07; /* 2022 */
  payload[5] = 6;    /* month */
  payload[6] = 15;   /* day */
  payload[7] = 10;   /* hour */
  payload[8] = 30;   /* minute */
  payload[9] = 45;   /* second */
  /* timezone LE at [10..11] */
  payload[10] = 0x3C;
  payload[11] = 0x00; /* +60 minutes */
  payload[12] = 0;    /* critical_battery_state */
  payload[13] = 0;    /* lock_n_go_timer */
  payload[14] = 0x01; /* last_lock_action */

  sdf_nuki_message_t msg = {.command_id = SDF_NUKI_CMD_KEYTURNER_STATES,
                            .payload = payload,
                            .payload_len = 15};

  sdf_keyturner_state_t state;
  TEST_ASSERT_EQUAL(SDF_NUKI_RESULT_OK,
                    sdf_nuki_parse_keyturner_states(&msg, &state));
  TEST_ASSERT_EQUAL_UINT8(0x02, state.nuki_state);
  TEST_ASSERT_EQUAL_UINT8(0x03, state.lock_state);
  TEST_ASSERT_EQUAL_UINT8(0x01, state.trigger);
  TEST_ASSERT_EQUAL_UINT16(2022, state.current_time_year);
  TEST_ASSERT_EQUAL_UINT8(6, state.current_time_month);
  TEST_ASSERT_EQUAL_UINT8(15, state.current_time_day);
  TEST_ASSERT_EQUAL_UINT8(10, state.current_time_hour);
  TEST_ASSERT_EQUAL_UINT8(30, state.current_time_minute);
  TEST_ASSERT_EQUAL_UINT8(45, state.current_time_second);
  TEST_ASSERT_EQUAL_INT16(60, state.timezone_offset_minutes);
  TEST_ASSERT_EQUAL_UINT8(0x01, state.last_lock_action);
  TEST_ASSERT_FALSE(state.has_last_lock_action_trigger);
  TEST_ASSERT_FALSE(state.has_door_sensor_state);
}

void test_parse_keyturner_states_extended(void) {
  /* Extended payload with optional fields: 19 bytes */
  uint8_t payload[19];
  memset(payload, 0, sizeof(payload));
  payload[0] = 0x04;  /* nuki_state */
  payload[1] = 0x01;  /* lock_state */
  payload[15] = 0x02; /* last_lock_action_trigger */
  payload[16] = 0x03; /* last_lock_action_completion_status */
  payload[17] = 0x01; /* door_sensor_state */
  payload[18] = 0x01; /* nightmode_active */

  sdf_nuki_message_t msg = {.command_id = SDF_NUKI_CMD_KEYTURNER_STATES,
                            .payload = payload,
                            .payload_len = 19};

  sdf_keyturner_state_t state;
  TEST_ASSERT_EQUAL(SDF_NUKI_RESULT_OK,
                    sdf_nuki_parse_keyturner_states(&msg, &state));
  TEST_ASSERT_TRUE(state.has_last_lock_action_trigger);
  TEST_ASSERT_EQUAL_UINT8(0x02, state.last_lock_action_trigger);
  TEST_ASSERT_TRUE(state.has_last_lock_action_completion_status);
  TEST_ASSERT_EQUAL_UINT8(0x03, state.last_lock_action_completion_status);
  TEST_ASSERT_TRUE(state.has_door_sensor_state);
  TEST_ASSERT_EQUAL_UINT8(0x01, state.door_sensor_state);
  TEST_ASSERT_TRUE(state.has_nightmode_active);
  TEST_ASSERT_EQUAL_UINT8(0x01, state.nightmode_active);
}

void test_parse_keyturner_states_too_short(void) {
  uint8_t payload[10] = {0};
  sdf_nuki_message_t msg = {.command_id = SDF_NUKI_CMD_KEYTURNER_STATES,
                            .payload = payload,
                            .payload_len = 10};

  sdf_keyturner_state_t state;
  TEST_ASSERT_EQUAL(SDF_NUKI_RESULT_ERR_ARG,
                    sdf_nuki_parse_keyturner_states(&msg, &state));
}

/* ---------- sdf_nuki_parse_error_report ---------- */

void test_parse_error_report_success(void) {
  uint8_t payload[3];
  payload[0] = 0x45; /* error_code: K_ERROR_BUSY */
  payload[1] = 0x0D; /* command_identifier LE low */
  payload[2] = 0x00; /* command_identifier LE high */

  sdf_nuki_message_t msg = {.command_id = SDF_NUKI_CMD_ERROR_REPORT,
                            .payload = payload,
                            .payload_len = 3};

  sdf_error_report_t report;
  TEST_ASSERT_EQUAL(SDF_NUKI_RESULT_OK,
                    sdf_nuki_parse_error_report(&msg, &report));
  TEST_ASSERT_EQUAL_INT8(0x45, report.error_code);
  TEST_ASSERT_EQUAL_UINT16(0x000D, report.command_identifier);
}

void test_parse_error_report_too_short(void) {
  uint8_t payload[2] = {0x45, 0x0D};
  sdf_nuki_message_t msg = {.command_id = SDF_NUKI_CMD_ERROR_REPORT,
                            .payload = payload,
                            .payload_len = 2};

  sdf_error_report_t report;
  TEST_ASSERT_EQUAL(SDF_NUKI_RESULT_ERR_ARG,
                    sdf_nuki_parse_error_report(&msg, &report));
}

/* ---------- sdf_nuki_compute_authenticator ---------- */

void test_compute_authenticator_deterministic(void) {
  uint8_t data[] = {1, 2, 3, 4, 5};
  uint8_t key[32];
  memset(key, 0xDD, sizeof(key));

  uint8_t out1[32], out2[32];
  TEST_ASSERT_EQUAL(SDF_NUKI_RESULT_OK, sdf_nuki_compute_authenticator(
                                            data, sizeof(data), key, out1));
  TEST_ASSERT_EQUAL(SDF_NUKI_RESULT_OK, sdf_nuki_compute_authenticator(
                                            data, sizeof(data), key, out2));
  TEST_ASSERT_EQUAL_MEMORY(out1, out2, 32);
}

void test_compute_authenticator_null_args(void) {
  uint8_t data[4] = {0}, key[32] = {0}, out[32];

  TEST_ASSERT_EQUAL(SDF_NUKI_RESULT_ERR_ARG,
                    sdf_nuki_compute_authenticator(NULL, 4, key, out));
  TEST_ASSERT_EQUAL(SDF_NUKI_RESULT_ERR_ARG,
                    sdf_nuki_compute_authenticator(data, 4, NULL, out));
  TEST_ASSERT_EQUAL(SDF_NUKI_RESULT_ERR_ARG,
                    sdf_nuki_compute_authenticator(data, 4, key, NULL));
}

void test_compute_authenticator_different_data(void) {
  uint8_t key[32];
  memset(key, 0xDD, sizeof(key));

  uint8_t data1[] = {1, 2, 3};
  uint8_t data2[] = {4, 5, 6};
  uint8_t out1[32], out2[32];

  sdf_nuki_compute_authenticator(data1, sizeof(data1), key, out1);
  sdf_nuki_compute_authenticator(data2, sizeof(data2), key, out2);
  TEST_ASSERT_FALSE(memcmp(out1, out2, 32) == 0);
}

/* ---------- sdf_nuki_compute_shared_key ---------- */

void test_compute_shared_key_null_args(void) {
  uint8_t priv[32] = {0}, pub[32] = {0}, out[32];

  TEST_ASSERT_EQUAL(SDF_NUKI_RESULT_ERR_ARG,
                    sdf_nuki_compute_shared_key(NULL, pub, out));
  TEST_ASSERT_EQUAL(SDF_NUKI_RESULT_ERR_ARG,
                    sdf_nuki_compute_shared_key(priv, NULL, out));
  TEST_ASSERT_EQUAL(SDF_NUKI_RESULT_ERR_ARG,
                    sdf_nuki_compute_shared_key(priv, pub, NULL));
}

/* ---------- Encrypt / Decrypt round-trip ---------- */

void test_encrypt_decrypt_round_trip(void) {
  sdf_nuki_client_t sender, receiver;
  uint32_t auth_id = 9999;
  uint8_t shared_key[32];
  memset(shared_key, 0xEE, sizeof(shared_key));

  sdf_nuki_credentials_t creds;
  memset(&creds, 0, sizeof(creds));
  creds.authorization_id = auth_id;
  creds.app_id = 1;
  memcpy(creds.shared_key, shared_key, 32);

  reset_mocks();
  sdf_nuki_client_init(&sender, &creds, mock_send_encrypted, NULL,
                       mock_send_unencrypted, NULL, NULL, NULL);
  sdf_nuki_client_init(&receiver, &creds, mock_send_encrypted, NULL,
                       mock_send_unencrypted, NULL, mock_message_cb, NULL);

  /* Send an encrypted command from sender */
  reset_mocks();
  int res = sdf_nuki_client_send_encrypted_custom(&sender, auth_id, shared_key,
                                                  SDF_NUKI_CMD_STATUS,
                                                  (const uint8_t *)"\x00", 1);
  TEST_ASSERT_EQUAL(SDF_NUKI_RESULT_OK, res);
  TEST_ASSERT_GREATER_THAN(0, (int)s_sent_len);

  /* Feed the encrypted data into receiver */
  res = sdf_nuki_client_feed_encrypted(&receiver, s_sent_data, s_sent_len);
  TEST_ASSERT_EQUAL(SDF_NUKI_RESULT_OK, res);
  TEST_ASSERT_EQUAL(1, s_msg_count);
  TEST_ASSERT_EQUAL_UINT16(SDF_NUKI_CMD_STATUS, s_received_msg.command_id);
}

void test_nonce_replay_detection(void) {
  sdf_nuki_client_t sender, receiver;
  uint32_t auth_id = 5555;
  uint8_t shared_key[32];
  memset(shared_key, 0xAA, sizeof(shared_key));

  sdf_nuki_credentials_t creds;
  memset(&creds, 0, sizeof(creds));
  creds.authorization_id = auth_id;
  memcpy(creds.shared_key, shared_key, 32);

  reset_mocks();
  sdf_nuki_client_init(&sender, &creds, mock_send_encrypted, NULL,
                       mock_send_unencrypted, NULL, NULL, NULL);
  sdf_nuki_client_init(&receiver, &creds, mock_send_encrypted, NULL,
                       mock_send_unencrypted, NULL, mock_message_cb, NULL);

  /* Send an encrypted command */
  reset_mocks();
  sdf_nuki_client_send_encrypted_custom(&sender, auth_id, shared_key,
                                        SDF_NUKI_CMD_STATUS,
                                        (const uint8_t *)"\x01", 1);

  /* Save the sent data */
  uint8_t saved_data[2048];
  size_t saved_len = s_sent_len;
  memcpy(saved_data, s_sent_data, saved_len);

  /* First feed: should succeed */
  int res = sdf_nuki_client_feed_encrypted(&receiver, saved_data, saved_len);
  TEST_ASSERT_EQUAL(SDF_NUKI_RESULT_OK, res);

  /* Second feed with same data: should detect nonce replay */
  res = sdf_nuki_client_feed_encrypted(&receiver, saved_data, saved_len);
  TEST_ASSERT_EQUAL(SDF_NUKI_RESULT_ERR_NONCE_REUSE, res);
}

void test_feed_encrypted_partial_data(void) {
  sdf_nuki_client_t client;
  make_client(&client, 1234);

  /* Feed only 10 bytes — not enough for adata header */
  uint8_t partial[10];
  memset(partial, 0, sizeof(partial));
  int res = sdf_nuki_client_feed_encrypted(&client, partial, sizeof(partial));
  TEST_ASSERT_EQUAL(SDF_NUKI_RESULT_ERR_INCOMPLETE, res);
}

void test_feed_encrypted_null_args(void) {
  sdf_nuki_client_t client;
  make_client(&client, 1234);
  uint8_t data[10] = {0};

  TEST_ASSERT_EQUAL(SDF_NUKI_RESULT_ERR_ARG,
                    sdf_nuki_client_feed_encrypted(NULL, data, sizeof(data)));
  TEST_ASSERT_EQUAL(SDF_NUKI_RESULT_ERR_ARG,
                    sdf_nuki_client_feed_encrypted(&client, NULL, 10));
  TEST_ASSERT_EQUAL(SDF_NUKI_RESULT_ERR_ARG,
                    sdf_nuki_client_feed_encrypted(&client, data, 0));
}

/* ---------- sdf_nuki_client_send_unencrypted ---------- */

void test_send_unencrypted_sends_framed_message(void) {
  sdf_nuki_client_t client;
  make_client(&client, 1234);

  uint8_t payload[] = {0x03, 0x00}; /* Request public key */
  int res = sdf_nuki_client_send_unencrypted(&client, SDF_NUKI_CMD_REQUEST_DATA,
                                             payload, sizeof(payload));
  TEST_ASSERT_EQUAL(SDF_NUKI_RESULT_OK, res);
  TEST_ASSERT_EQUAL(1, s_send_count);
  /* Framed message: 2 (command) + 2 (payload) + 2 (CRC) = 6 bytes */
  TEST_ASSERT_EQUAL(6, (int)s_sent_len);
}
