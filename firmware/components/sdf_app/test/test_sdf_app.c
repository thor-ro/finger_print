#include "unity.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "sdf_app.h"
#include "sdf_power.h"
#include "sdf_protocol_ble.h"
#include "sdf_protocol_zigbee.h"
#include "sdf_services.h"

/* Exposed via SDF_APP_TESTING */
extern const char *test_sdf_app_status_name(uint8_t status);
extern const char *test_sdf_app_zb_programming_cmd_name(uint8_t cmd_id);
extern const char *
test_sdf_app_enrollment_state_name(sdf_enrollment_state_t state);
extern const char *
test_sdf_app_enrollment_result_name(sdf_enrollment_result_t result);
extern const char *
test_sdf_app_power_wake_reason_name(sdf_power_wake_reason_t reason);
extern const char *test_sdf_app_error_name(int8_t code);
extern const char *test_sdf_app_audit_event_name(sdf_audit_event_type_t type);
extern bool test_sdf_app_valid_lock_action(uint8_t lock_action);
extern sdf_protocol_zigbee_lock_state_t
test_sdf_app_map_lock_state_to_zigbee(uint8_t nuki_lock_state);
extern uint8_t test_sdf_app_choose_fingerprint_permission(
    const sdf_protocol_zigbee_programming_event_t *pe);

void test_sdf_app_string_mappers(void) {
  /* test_sdf_app_status_name */
  TEST_ASSERT_EQUAL_STRING("ACCEPTED",
                           test_sdf_app_status_name(SDF_STATUS_ACCEPTED));
  TEST_ASSERT_EQUAL_STRING("COMPLETE",
                           test_sdf_app_status_name(SDF_STATUS_COMPLETE));
  TEST_ASSERT_EQUAL_STRING("UNKNOWN", test_sdf_app_status_name(0xFF));

  /* test_sdf_app_zb_programming_cmd_name */
  TEST_ASSERT_EQUAL_STRING("SET_PIN_CODE",
                           test_sdf_app_zb_programming_cmd_name(0x05));
  TEST_ASSERT_EQUAL_STRING("CLEAR_PIN_CODE",
                           test_sdf_app_zb_programming_cmd_name(0x07));
  TEST_ASSERT_EQUAL_STRING("UNKNOWN",
                           test_sdf_app_zb_programming_cmd_name(0xFF));

  /* test_sdf_app_enrollment_state_name */
  TEST_ASSERT_EQUAL_STRING(
      "IDLE", test_sdf_app_enrollment_state_name(SDF_ENROLLMENT_STATE_IDLE));
  TEST_ASSERT_EQUAL_STRING("STEP_1", test_sdf_app_enrollment_state_name(
                                         SDF_ENROLLMENT_STATE_STEP_1));
  TEST_ASSERT_EQUAL_STRING("SUCCESS", test_sdf_app_enrollment_state_name(
                                          SDF_ENROLLMENT_STATE_SUCCESS));
  TEST_ASSERT_EQUAL_STRING("UNKNOWN", test_sdf_app_enrollment_state_name(0xFF));

  /* test_sdf_app_enrollment_result_name */
  TEST_ASSERT_EQUAL_STRING(
      "NONE", test_sdf_app_enrollment_result_name(SDF_ENROLLMENT_RESULT_NONE));
  TEST_ASSERT_EQUAL_STRING("SUCCESS", test_sdf_app_enrollment_result_name(
                                          SDF_ENROLLMENT_RESULT_SUCCESS));
  TEST_ASSERT_EQUAL_STRING("UNKNOWN",
                           test_sdf_app_enrollment_result_name(0xFF));

  /* test_sdf_app_power_wake_reason_name */
  TEST_ASSERT_EQUAL_STRING("timer", test_sdf_app_power_wake_reason_name(
                                        SDF_POWER_WAKE_REASON_TIMER));
  TEST_ASSERT_EQUAL_STRING(
      "fingerprint",
      test_sdf_app_power_wake_reason_name(SDF_POWER_WAKE_REASON_FINGERPRINT));
  TEST_ASSERT_EQUAL_STRING(
      "none", test_sdf_app_power_wake_reason_name(SDF_POWER_WAKE_REASON_NONE));
  /* using unknown enum causes it to default to "none" */
  TEST_ASSERT_EQUAL_STRING("none", test_sdf_app_power_wake_reason_name(0xFF));

  /* test_sdf_app_error_name */
  TEST_ASSERT_EQUAL_STRING("K_ERROR_BUSY", test_sdf_app_error_name(0x45));
  TEST_ASSERT_EQUAL_STRING("UNKNOWN_ERROR", test_sdf_app_error_name(0x00));

  /* test_sdf_app_audit_event_name */
  TEST_ASSERT_EQUAL_STRING(
      "STORAGE_POLICY_OK",
      test_sdf_app_audit_event_name(SDF_AUDIT_STORAGE_POLICY_OK));
  TEST_ASSERT_EQUAL_STRING("UNKNOWN", test_sdf_app_audit_event_name(0xFF));
}

void test_sdf_app_valid_lock_action_logic(void) {
  TEST_ASSERT_TRUE(test_sdf_app_valid_lock_action(SDF_LOCK_ACTION_UNLOCK));
  TEST_ASSERT_TRUE(test_sdf_app_valid_lock_action(SDF_LOCK_ACTION_LOCK));
  TEST_ASSERT_TRUE(test_sdf_app_valid_lock_action(SDF_LOCK_ACTION_UNLATCH));
  TEST_ASSERT_TRUE(test_sdf_app_valid_lock_action(SDF_LOCK_ACTION_LOCK_N_GO));
  TEST_ASSERT_TRUE(
      test_sdf_app_valid_lock_action(SDF_LOCK_ACTION_LOCK_N_GO_UNLATCH));
  TEST_ASSERT_TRUE(test_sdf_app_valid_lock_action(SDF_LOCK_ACTION_FULL_LOCK));

  /* Invalid actions */
  TEST_ASSERT_FALSE(test_sdf_app_valid_lock_action(0x00));
  TEST_ASSERT_FALSE(test_sdf_app_valid_lock_action(0xFF));
  TEST_ASSERT_FALSE(test_sdf_app_valid_lock_action(0x08)); /* Unknown */
}

void test_sdf_app_map_lock_state_to_zigbee_logic(void) {
  /* Locked */
  TEST_ASSERT_EQUAL(SDF_PROTOCOL_ZIGBEE_LOCK_STATE_LOCKED,
                    test_sdf_app_map_lock_state_to_zigbee(0x01));

  /* Unlocked */
  TEST_ASSERT_EQUAL(SDF_PROTOCOL_ZIGBEE_LOCK_STATE_UNLOCKED,
                    test_sdf_app_map_lock_state_to_zigbee(0x03));
  TEST_ASSERT_EQUAL(
      SDF_PROTOCOL_ZIGBEE_LOCK_STATE_UNLOCKED,
      test_sdf_app_map_lock_state_to_zigbee(0x05)); /* unlatched */
  TEST_ASSERT_EQUAL(
      SDF_PROTOCOL_ZIGBEE_LOCK_STATE_UNLOCKED,
      test_sdf_app_map_lock_state_to_zigbee(0x06)); /* unlocked (lock n go) */

  /* Not fully locked */
  TEST_ASSERT_EQUAL(
      SDF_PROTOCOL_ZIGBEE_LOCK_STATE_NOT_FULLY_LOCKED,
      test_sdf_app_map_lock_state_to_zigbee(0x02)); /* unlocking */
  TEST_ASSERT_EQUAL(SDF_PROTOCOL_ZIGBEE_LOCK_STATE_NOT_FULLY_LOCKED,
                    test_sdf_app_map_lock_state_to_zigbee(0x04)); /* locking */
  TEST_ASSERT_EQUAL(
      SDF_PROTOCOL_ZIGBEE_LOCK_STATE_NOT_FULLY_LOCKED,
      test_sdf_app_map_lock_state_to_zigbee(0x07)); /* unlatching */

  /* Undefined */
  TEST_ASSERT_EQUAL(SDF_PROTOCOL_ZIGBEE_LOCK_STATE_UNDEFINED,
                    test_sdf_app_map_lock_state_to_zigbee(0x00));
  TEST_ASSERT_EQUAL(SDF_PROTOCOL_ZIGBEE_LOCK_STATE_UNDEFINED,
                    test_sdf_app_map_lock_state_to_zigbee(0xFF));
}

void test_sdf_app_choose_fingerprint_permission_logic(void) {
  sdf_protocol_zigbee_programming_event_t pe;
  memset(&pe, 0, sizeof(pe));

  /* Default when PE is null is 1 */
  TEST_ASSERT_EQUAL_UINT8(1, test_sdf_app_choose_fingerprint_permission(NULL));

  /* Default when PE has no user_type or user_status is 1 */
  TEST_ASSERT_EQUAL_UINT8(1, test_sdf_app_choose_fingerprint_permission(&pe));

  /* Prefers user_type over user_status if it is valid (1, 2, 3) */
  pe.has_user_type = true;
  pe.user_type = 2;
  pe.has_user_status = true;
  pe.user_status = 3;
  TEST_ASSERT_EQUAL_UINT8(2, test_sdf_app_choose_fingerprint_permission(&pe));

  /* If user_type is invalid, falls back to user_status */
  pe.user_type = 4;
  TEST_ASSERT_EQUAL_UINT8(3, test_sdf_app_choose_fingerprint_permission(&pe));

  /* If both are invalid, falls back to 1 */
  pe.user_status = 0;
  TEST_ASSERT_EQUAL_UINT8(1, test_sdf_app_choose_fingerprint_permission(&pe));

  /* If user_type is not present, uses user_status */
  pe.has_user_type = false;
  pe.user_status = 3;
  TEST_ASSERT_EQUAL_UINT8(3, test_sdf_app_choose_fingerprint_permission(&pe));
}
