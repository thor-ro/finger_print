/* Host (linux target) unit tests for the Enrollment characteristic's
 * user-management request/reply protocol (companion-user-mgmt): request
 * parsing, reply/list-part formatting, the setup-phase admission decision,
 * and list chunking with its explicit end marker.
 *
 * The code under test lives in sdf_ble_companion_user_mgmt.c, which is
 * compiled straight into this runner on the linux target (see
 * test_runner/main/CMakeLists.txt) because sdf_ble_companion itself needs
 * the BLE stack. */

#include "unity.h"

#include <string.h>

#include "sdf_ble_companion.h"
#include "sdf_ble_companion_gatt_scratch.h"

/* --- Parsing ------------------------------------------------------------- */

void test_um_parse_list_request(void) {
  const char *json = "{\"req\":7,\"verb\":\"list\"}";
  sdf_ble_companion_um_request_t req;
  TEST_ASSERT_TRUE(sdf_ble_companion_um_parse_request(
      (const uint8_t *)json, strlen(json), &req));
  TEST_ASSERT_TRUE(req.req_id_valid);
  TEST_ASSERT_EQUAL_UINT32(7, req.req_id);
  TEST_ASSERT_EQUAL_INT(SDF_BLE_COMPANION_UM_VERB_LIST, req.verb);
}

void test_um_parse_enroll_request(void) {
  const char *json = "{\"req\":1,\"verb\":\"enroll\",\"user_id\":4,"
                     "\"permission\":2}";
  sdf_ble_companion_um_request_t req;
  TEST_ASSERT_TRUE(sdf_ble_companion_um_parse_request(
      (const uint8_t *)json, strlen(json), &req));
  TEST_ASSERT_EQUAL_INT(SDF_BLE_COMPANION_UM_VERB_ENROLL, req.verb);
  TEST_ASSERT_EQUAL_UINT16(4, req.user_id);
  TEST_ASSERT_EQUAL_UINT8(2, req.permission);
}

void test_um_parse_delete_set_permission_rename(void) {
  sdf_ble_companion_um_request_t req;

  const char *del = "{\"req\":2,\"verb\":\"delete\",\"user_id\":3}";
  TEST_ASSERT_TRUE(sdf_ble_companion_um_parse_request(
      (const uint8_t *)del, strlen(del), &req));
  TEST_ASSERT_EQUAL_INT(SDF_BLE_COMPANION_UM_VERB_DELETE, req.verb);
  TEST_ASSERT_EQUAL_UINT16(3, req.user_id);

  const char *perm = "{\"req\":3,\"verb\":\"set_permission\",\"user_id\":5,"
                     "\"permission\":1}";
  TEST_ASSERT_TRUE(sdf_ble_companion_um_parse_request(
      (const uint8_t *)perm, strlen(perm), &req));
  TEST_ASSERT_EQUAL_INT(SDF_BLE_COMPANION_UM_VERB_SET_PERMISSION, req.verb);
  TEST_ASSERT_EQUAL_UINT16(5, req.user_id);
  TEST_ASSERT_EQUAL_UINT8(1, req.permission);

  const char *ren = "{\"req\":4,\"verb\":\"rename\",\"user_id\":6,"
                    "\"name\":\"Bob\"}";
  TEST_ASSERT_TRUE(sdf_ble_companion_um_parse_request(
      (const uint8_t *)ren, strlen(ren), &req));
  TEST_ASSERT_EQUAL_INT(SDF_BLE_COMPANION_UM_VERB_RENAME, req.verb);
  TEST_ASSERT_EQUAL_STRING("Bob", req.name);
}

void test_um_parse_malformed_json_captures_req_id(void) {
  /* Broken payload that still carries a parseable req first: the caller can
   * answer invalid with correlation instead of dropping it silently. */
  const char *json = "{\"req\":9,\"verb\":\"delete\",\"user_id\":";
  sdf_ble_companion_um_request_t req;
  TEST_ASSERT_FALSE(sdf_ble_companion_um_parse_request(
      (const uint8_t *)json, strlen(json), &req));
  TEST_ASSERT_TRUE(req.req_id_valid);
  TEST_ASSERT_EQUAL_UINT32(9, req.req_id);
}

void test_um_parse_malformed_json_without_req_id(void) {
  sdf_ble_companion_um_request_t req;
  const char *garbage = "not json at all";
  TEST_ASSERT_FALSE(sdf_ble_companion_um_parse_request(
      (const uint8_t *)garbage, strlen(garbage), &req));
  TEST_ASSERT_FALSE(req.req_id_valid);

  const char *empty = "{}";
  TEST_ASSERT_FALSE(sdf_ble_companion_um_parse_request(
      (const uint8_t *)empty, strlen(empty), &req));
}

void test_um_parse_legacy_bare_enroll_payload_rejected(void) {
  /* The old fire-and-forget shape stops being accepted: no verb, no
   * request id -> answered upstream as an invalid request, never executed.
   * In particular an admin-permission enrolment cannot be smuggled through
   * the legacy shape. */
  sdf_ble_companion_um_request_t req;
  const char *legacy = "{\"user_id\":1,\"permission\":3}";
  TEST_ASSERT_FALSE(sdf_ble_companion_um_parse_request(
      (const uint8_t *)legacy, strlen(legacy), &req));
}

void test_um_parse_unknown_verb_rejected(void) {
  sdf_ble_companion_um_request_t req;
  const char *json = "{\"req\":5,\"verb\":\"factory_reset\"}";
  TEST_ASSERT_FALSE(sdf_ble_companion_um_parse_request(
      (const uint8_t *)json, strlen(json), &req));
}

void test_um_parse_out_of_range_fields_rejected(void) {
  sdf_ble_companion_um_request_t req;
  const char *bad_id = "{\"req\":1,\"verb\":\"delete\",\"user_id\":11}";
  TEST_ASSERT_FALSE(sdf_ble_companion_um_parse_request(
      (const uint8_t *)bad_id, strlen(bad_id), &req));

  const char *zero_id = "{\"req\":1,\"verb\":\"delete\",\"user_id\":0}";
  TEST_ASSERT_FALSE(sdf_ble_companion_um_parse_request(
      (const uint8_t *)zero_id, strlen(zero_id), &req));

  const char *bad_perm = "{\"req\":1,\"verb\":\"enroll\",\"user_id\":2,"
                         "\"permission\":4}";
  TEST_ASSERT_FALSE(sdf_ble_companion_um_parse_request(
      (const uint8_t *)bad_perm, strlen(bad_perm), &req));

  const char *missing_field = "{\"req\":1,\"verb\":\"rename\",\"user_id\":2}";
  TEST_ASSERT_FALSE(sdf_ble_companion_um_parse_request(
      (const uint8_t *)missing_field, strlen(missing_field), &req));
}

void test_um_parse_trailing_garbage_rejected(void) {
  sdf_ble_companion_um_request_t req;
  const char *json = "{\"req\":1,\"verb\":\"list\"} trailing";
  TEST_ASSERT_FALSE(sdf_ble_companion_um_parse_request(
      (const uint8_t *)json, strlen(json), &req));
}

/* --- Reply formatting ----------------------------------------------------- */

void test_um_format_reply_shape(void) {
  char buf[64];
  int n = sdf_ble_companion_um_format_reply(12, "last_admin", buf,
                                            sizeof(buf));
  TEST_ASSERT_GREATER_THAN(0, n);
  TEST_ASSERT_EQUAL_STRING("{\"req\":12,\"result\":\"last_admin\"}", buf);
}

void test_um_format_reply_too_small_buffer_fails(void) {
  char buf[8];
  TEST_ASSERT_LESS_THAN(0, sdf_ble_companion_um_format_reply(
                               12, "last_admin", buf, sizeof(buf)));
}

void test_um_format_list_part_end_marker(void) {
  char buf[256];
  int n = sdf_ble_companion_um_format_list_part(
      3, 0, false, "[{\"id\":1,\"perm\":3,\"name\":\"Alice\"}]", buf,
      sizeof(buf));
  TEST_ASSERT_GREATER_THAN(0, n);
  TEST_ASSERT_EQUAL_STRING(
      "{\"req\":3,\"verb\":\"list\",\"part\":0,\"end\":false,"
      "\"users\":[{\"id\":1,\"perm\":3,\"name\":\"Alice\"}]}",
      buf);

  n = sdf_ble_companion_um_format_list_part(3, 1, true, "[]", buf,
                                            sizeof(buf));
  TEST_ASSERT_GREATER_THAN(0, n);
  TEST_ASSERT_NOT_NULL(strstr(buf, "\"end\":true"));
}

/* --- Admission decision ---------------------------------------------------- */

static sdf_ble_companion_um_request_t make_req(sdf_ble_companion_um_verb_t verb) {
  sdf_ble_companion_um_request_t req = {0};
  req.req_id = 1;
  req.req_id_valid = true;
  req.verb = verb;
  req.user_id = 2;
  req.permission = 3;
  strncpy(req.name, "X", sizeof(req.name) - 1);
  return req;
}

void test_um_admission_authority_admits_every_verb(void) {
  static const sdf_ble_companion_um_verb_t verbs[] = {
      SDF_BLE_COMPANION_UM_VERB_LIST,
      SDF_BLE_COMPANION_UM_VERB_ENROLL,
      SDF_BLE_COMPANION_UM_VERB_DELETE,
      SDF_BLE_COMPANION_UM_VERB_SET_PERMISSION,
      SDF_BLE_COMPANION_UM_VERB_RENAME,
  };
  for (size_t i = 0; i < sizeof(verbs) / sizeof(verbs[0]); i++) {
    sdf_ble_companion_um_request_t req = make_req(verbs[i]);
    TEST_ASSERT_TRUE_MESSAGE(
        sdf_ble_companion_um_admits(&req, true, false, false),
        "admin authority admits every verb");
  }
}

void test_um_admission_setup_phase_no_users_admits_only_enroll(void) {
  sdf_ble_companion_um_request_t enroll =
      make_req(SDF_BLE_COMPANION_UM_VERB_ENROLL);
  TEST_ASSERT_TRUE(sdf_ble_companion_um_admits(&enroll, false, true, true));

  /* Every other verb refused on the same connection. */
  sdf_ble_companion_um_request_t list = make_req(SDF_BLE_COMPANION_UM_VERB_LIST);
  TEST_ASSERT_FALSE(sdf_ble_companion_um_admits(&list, false, true, true));
  sdf_ble_companion_um_request_t del = make_req(SDF_BLE_COMPANION_UM_VERB_DELETE);
  TEST_ASSERT_FALSE(sdf_ble_companion_um_admits(&del, false, true, true));
  sdf_ble_companion_um_request_t perm =
      make_req(SDF_BLE_COMPANION_UM_VERB_SET_PERMISSION);
  TEST_ASSERT_FALSE(sdf_ble_companion_um_admits(&perm, false, true, true));
  sdf_ble_companion_um_request_t ren = make_req(SDF_BLE_COMPANION_UM_VERB_RENAME);
  TEST_ASSERT_FALSE(sdf_ble_companion_um_admits(&ren, false, true, true));
}

void test_um_admission_setup_phase_closes_once_any_user_enrolled(void) {
  sdf_ble_companion_um_request_t enroll =
      make_req(SDF_BLE_COMPANION_UM_VERB_ENROLL);
  /* Still in the setup phase but one user now exists: the exception is
   * over - a second admin could otherwise be added to a claimed device. */
  TEST_ASSERT_FALSE(sdf_ble_companion_um_admits(&enroll, false, true, false));
}

void test_um_admission_outside_setup_requires_authority(void) {
  sdf_ble_companion_um_request_t enroll =
      make_req(SDF_BLE_COMPANION_UM_VERB_ENROLL);
  TEST_ASSERT_FALSE(sdf_ble_companion_um_admits(&enroll, false, false, true));
  TEST_ASSERT_FALSE(sdf_ble_companion_um_admits(&enroll, false, false, false));
}

/* --- Degenerate writes leave the caller a defined request ---------------
 *
 * dispatch_enroll_write() reads req.req_id_valid on the parser's failure
 * path to echo the request id in its error reply, and the Enrollment
 * characteristic admits a zero-length write - so the parser must clear the
 * struct before any early return, not after. */

void test_um_parse_rejects_empty_write_and_still_clears_request(void) {
  sdf_ble_companion_um_request_t req;
  memset(&req, 0xAB, sizeof(req));

  TEST_ASSERT_FALSE(sdf_ble_companion_um_parse_request((const uint8_t *)"", 0,
                                                       &req));
  TEST_ASSERT_FALSE(req.req_id_valid);
  TEST_ASSERT_EQUAL_UINT32(0, req.req_id);
}

void test_um_parse_rejects_null_data_and_still_clears_request(void) {
  sdf_ble_companion_um_request_t req;
  memset(&req, 0xAB, sizeof(req));

  TEST_ASSERT_FALSE(sdf_ble_companion_um_parse_request(NULL, 4, &req));
  TEST_ASSERT_FALSE(req.req_id_valid);
  TEST_ASSERT_EQUAL_UINT32(0, req.req_id);
}

void test_um_parse_rejects_oversized_write_and_still_clears_request(void) {
  /* The parser caps a write at the GATT staging buffer length. */
  static uint8_t big[SDF_BLE_COMPANION_GATT_SCRATCH_LEN + 8];
  memset(big, '{', sizeof(big));
  sdf_ble_companion_um_request_t req;
  memset(&req, 0xAB, sizeof(req));

  TEST_ASSERT_FALSE(
      sdf_ble_companion_um_parse_request(big, sizeof(big), &req));
  TEST_ASSERT_FALSE(req.req_id_valid);
}

void test_um_parse_rejects_absurd_request_id_rather_than_clamping(void) {
  /* A clamped id would be echoed back as a number the client never sent. */
  const char *json = "{\"req_id\":99999999999,\"verb\":\"list\"}";
  sdf_ble_companion_um_request_t req;
  TEST_ASSERT_FALSE(sdf_ble_companion_um_parse_request(
      (const uint8_t *)json, strlen(json), &req));
}

/* --- Config-characteristic admission ------------------------------------- */

/* Regression cover for the defect these tests could not previously see: the
 * Config and Enrollment access callbacks applied a blanket admin gate ahead
 * of the dispatchers, so both admission decisions were unreachable on the
 * unauthenticated setup connection they exist for. The policy now lives in
 * the dispatcher; these assert the policy, and the callbacks now consult it.
 * Reachability itself is a wire-level property - see the BLE harness. */

void test_config_admits_every_kind_for_a_live_admin(void) {
  TEST_ASSERT_TRUE(sdf_ble_companion_config_admits(
      SDF_BLE_COMPANION_CONFIG_WRITE_PRIVILEGED, true, false, true));
  TEST_ASSERT_TRUE(sdf_ble_companion_config_admits(
      SDF_BLE_COMPANION_CONFIG_WRITE_SETUP_NUKI_PAIR, true, false, true));
  TEST_ASSERT_TRUE(sdf_ble_companion_config_admits(
      SDF_BLE_COMPANION_CONFIG_WRITE_FINISH_SETUP, true, false, true));
}

void test_config_admits_wizard_requests_during_an_armed_setup_phase(void) {
  /* No account and no admin exists yet, which is exactly why no authority
   * can be required here. */
  TEST_ASSERT_TRUE(sdf_ble_companion_config_admits(
      SDF_BLE_COMPANION_CONFIG_WRITE_SETUP_NUKI_PAIR, false, true, false));
  TEST_ASSERT_TRUE(sdf_ble_companion_config_admits(
      SDF_BLE_COMPANION_CONFIG_WRITE_FINISH_SETUP, false, true, false));
}

void test_config_refuses_privileged_writes_without_authority(void) {
  /* Admin-action triggers and plain config mutations stay admin-only even
   * mid-setup: relaxing the callback gate must not hand an unauthenticated
   * peer a factory reset. */
  TEST_ASSERT_FALSE(sdf_ble_companion_config_admits(
      SDF_BLE_COMPANION_CONFIG_WRITE_PRIVILEGED, false, true, false));
}

void test_config_refuses_wizard_requests_once_setup_is_complete(void) {
  /* The completion latch closes the exception for a device in service. */
  TEST_ASSERT_FALSE(sdf_ble_companion_config_admits(
      SDF_BLE_COMPANION_CONFIG_WRITE_SETUP_NUKI_PAIR, false, true, true));
  TEST_ASSERT_FALSE(sdf_ble_companion_config_admits(
      SDF_BLE_COMPANION_CONFIG_WRITE_FINISH_SETUP, false, true, true));
}

void test_config_refuses_wizard_requests_when_the_phase_is_disarmed(void) {
  /* A lapsed setup phase is not an open door. */
  TEST_ASSERT_FALSE(sdf_ble_companion_config_admits(
      SDF_BLE_COMPANION_CONFIG_WRITE_FINISH_SETUP, false, false, false));
}
