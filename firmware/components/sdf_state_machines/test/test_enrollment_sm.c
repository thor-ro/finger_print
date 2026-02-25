#include "sdf_state_machines.h"
#include "unity.h"

void test_enrollment_sm_initialization(void) {
  sdf_enrollment_sm_t sm;
  sdf_enrollment_sm_init(&sm);

  TEST_ASSERT_EQUAL(SDF_ENROLLMENT_STATE_IDLE, sm.state);
  TEST_ASSERT_EQUAL(SDF_ENROLLMENT_RESULT_NONE, sm.result);
  TEST_ASSERT_EQUAL(0, sm.user_id);
  TEST_ASSERT_EQUAL(0, sm.permission);
  TEST_ASSERT_EQUAL(0, sm.completed_steps);
}

void test_enrollment_sm_start_valid(void) {
  sdf_enrollment_sm_t sm;
  sdf_enrollment_sm_init(&sm);

  esp_err_t err = sdf_enrollment_sm_start(&sm, 5, 2);
  TEST_ASSERT_EQUAL(ESP_OK, err);
  TEST_ASSERT_EQUAL(SDF_ENROLLMENT_STATE_STEP_1, sm.state);
  TEST_ASSERT_EQUAL(SDF_ENROLLMENT_RESULT_NONE, sm.result);
  TEST_ASSERT_EQUAL(5, sm.user_id);
  TEST_ASSERT_EQUAL(2, sm.permission);
  TEST_ASSERT_EQUAL(0, sm.completed_steps);
}

void test_enrollment_sm_start_invalid(void) {
  sdf_enrollment_sm_t sm;
  sdf_enrollment_sm_init(&sm);

  // Invalid User ID (too low)
  esp_err_t err = sdf_enrollment_sm_start(&sm, 0, 1);
  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);

  // Invalid User ID (too high)
  err = sdf_enrollment_sm_start(&sm, 5000, 1);
  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);

  // Invalid Permission (too low)
  err = sdf_enrollment_sm_start(&sm, 5, 0);
  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);

  // Invalid Permission (too high)
  err = sdf_enrollment_sm_start(&sm, 5, 4);
  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);
}

void test_enrollment_sm_success_sequence(void) {
  sdf_enrollment_sm_t sm;
  sdf_enrollment_sm_init(&sm);
  sdf_enrollment_sm_start(&sm, 1, 3);

  // Step 1 OK
  sdf_enrollment_sm_apply_step_result(&sm, SDF_FINGERPRINT_OP_OK);
  TEST_ASSERT_EQUAL(SDF_ENROLLMENT_STATE_STEP_2, sm.state);
  TEST_ASSERT_EQUAL(1, sm.completed_steps);

  // Step 2 OK
  sdf_enrollment_sm_apply_step_result(&sm, SDF_FINGERPRINT_OP_OK);
  TEST_ASSERT_EQUAL(SDF_ENROLLMENT_STATE_STEP_3, sm.state);
  TEST_ASSERT_EQUAL(2, sm.completed_steps);

  // Step 3 OK -> SUCCESS
  sdf_enrollment_sm_apply_step_result(&sm, SDF_FINGERPRINT_OP_OK);
  TEST_ASSERT_EQUAL(SDF_ENROLLMENT_STATE_SUCCESS, sm.state);
  TEST_ASSERT_EQUAL(SDF_ENROLLMENT_RESULT_SUCCESS, sm.result);
  TEST_ASSERT_EQUAL(3, sm.completed_steps);
}

void test_enrollment_sm_failure_handling(void) {
  sdf_enrollment_sm_t sm;
  sdf_enrollment_sm_init(&sm);
  sdf_enrollment_sm_start(&sm, 10, 1);

  // Arbitrary failure on step 1
  sdf_enrollment_sm_apply_step_result(&sm, SDF_FINGERPRINT_OP_TIMEOUT);
  TEST_ASSERT_EQUAL(SDF_ENROLLMENT_STATE_ERROR, sm.state);
  TEST_ASSERT_EQUAL(SDF_ENROLLMENT_RESULT_TIMEOUT, sm.result);
}

void test_enrollment_sm_user_occupied(void) {
  sdf_enrollment_sm_t sm;
  sdf_enrollment_sm_init(&sm);
  sdf_enrollment_sm_start(&sm, 20, 2);

  sdf_enrollment_sm_apply_step_result(&sm, SDF_FINGERPRINT_OP_USER_OCCUPIED);
  TEST_ASSERT_EQUAL(SDF_ENROLLMENT_STATE_ERROR, sm.state);
  TEST_ASSERT_EQUAL(SDF_ENROLLMENT_RESULT_USER_OCCUPIED, sm.result);
}
