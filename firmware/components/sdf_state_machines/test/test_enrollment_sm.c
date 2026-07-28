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

/* ---------- is_active ---------- */

void test_enrollment_sm_is_active_idle(void) {
  sdf_enrollment_sm_t sm;
  sdf_enrollment_sm_init(&sm);
  TEST_ASSERT_FALSE(sdf_enrollment_sm_is_active(&sm));
}

void test_enrollment_sm_is_active_steps(void) {
  sdf_enrollment_sm_t sm;
  sdf_enrollment_sm_init(&sm);
  sdf_enrollment_sm_start(&sm, 1, 1);
  TEST_ASSERT_TRUE(sdf_enrollment_sm_is_active(&sm));

  sdf_enrollment_sm_apply_step_result(&sm, SDF_FINGERPRINT_OP_OK);
  TEST_ASSERT_TRUE(sdf_enrollment_sm_is_active(&sm)); /* STEP_2 */

  sdf_enrollment_sm_apply_step_result(&sm, SDF_FINGERPRINT_OP_OK);
  TEST_ASSERT_TRUE(sdf_enrollment_sm_is_active(&sm)); /* STEP_3 */

  sdf_enrollment_sm_apply_step_result(&sm, SDF_FINGERPRINT_OP_OK);
  TEST_ASSERT_FALSE(sdf_enrollment_sm_is_active(&sm)); /* SUCCESS */
}

void test_enrollment_sm_is_active_error(void) {
  sdf_enrollment_sm_t sm;
  sdf_enrollment_sm_init(&sm);
  sdf_enrollment_sm_start(&sm, 1, 1);
  sdf_enrollment_sm_apply_step_result(&sm, SDF_FINGERPRINT_OP_PROTOCOL_ERROR);
  TEST_ASSERT_FALSE(sdf_enrollment_sm_is_active(&sm));
}

/* ---------- current_step ---------- */

void test_enrollment_sm_current_step(void) {
  sdf_enrollment_sm_t sm;
  sdf_enrollment_sm_init(&sm);
  TEST_ASSERT_EQUAL_UINT8(0, sdf_enrollment_sm_current_step(&sm));

  sdf_enrollment_sm_start(&sm, 1, 1);
  TEST_ASSERT_EQUAL_UINT8(1, sdf_enrollment_sm_current_step(&sm));

  sdf_enrollment_sm_apply_step_result(&sm, SDF_FINGERPRINT_OP_OK);
  TEST_ASSERT_EQUAL_UINT8(2, sdf_enrollment_sm_current_step(&sm));

  sdf_enrollment_sm_apply_step_result(&sm, SDF_FINGERPRINT_OP_OK);
  TEST_ASSERT_EQUAL_UINT8(3, sdf_enrollment_sm_current_step(&sm));

  sdf_enrollment_sm_apply_step_result(&sm, SDF_FINGERPRINT_OP_OK);
  TEST_ASSERT_EQUAL_UINT8(0, sdf_enrollment_sm_current_step(&sm));
}

/* ---------- current_command ---------- */

void test_enrollment_sm_current_command(void) {
  sdf_enrollment_sm_t sm;
  sdf_enrollment_sm_init(&sm);
  TEST_ASSERT_EQUAL_UINT8(0, sdf_enrollment_sm_current_command(&sm));

  sdf_enrollment_sm_start(&sm, 1, 1);
  TEST_ASSERT_EQUAL_UINT8(0x01, sdf_enrollment_sm_current_command(&sm));

  sdf_enrollment_sm_apply_step_result(&sm, SDF_FINGERPRINT_OP_OK);
  TEST_ASSERT_EQUAL_UINT8(0x02, sdf_enrollment_sm_current_command(&sm));

  sdf_enrollment_sm_apply_step_result(&sm, SDF_FINGERPRINT_OP_OK);
  TEST_ASSERT_EQUAL_UINT8(0x03, sdf_enrollment_sm_current_command(&sm));

  sdf_enrollment_sm_apply_step_result(&sm, SDF_FINGERPRINT_OP_OK);
  TEST_ASSERT_EQUAL_UINT8(0, sdf_enrollment_sm_current_command(&sm));
}

/* ---------- reset ---------- */

void test_enrollment_sm_reset(void) {
  sdf_enrollment_sm_t sm;
  sdf_enrollment_sm_init(&sm);
  sdf_enrollment_sm_start(&sm, 42, 3);
  sdf_enrollment_sm_apply_step_result(&sm, SDF_FINGERPRINT_OP_OK);

  sdf_enrollment_sm_reset(&sm);
  TEST_ASSERT_EQUAL(SDF_ENROLLMENT_STATE_IDLE, sm.state);
  TEST_ASSERT_EQUAL(SDF_ENROLLMENT_RESULT_NONE, sm.result);
  TEST_ASSERT_EQUAL(0, sm.user_id);
  TEST_ASSERT_EQUAL(0, sm.completed_steps);
}

/* ---------- NULL safety ---------- */

void test_enrollment_sm_null_safety(void) {
  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, sdf_enrollment_sm_start(NULL, 1, 1));
  TEST_ASSERT_FALSE(sdf_enrollment_sm_is_active(NULL));
  TEST_ASSERT_EQUAL_UINT8(0, sdf_enrollment_sm_current_step(NULL));
  TEST_ASSERT_EQUAL_UINT8(0, sdf_enrollment_sm_current_command(NULL));
  /* apply_step_result with NULL should not crash */
  sdf_enrollment_sm_apply_step_result(NULL, SDF_FINGERPRINT_OP_OK);
  /* init with NULL should not crash */
  sdf_enrollment_sm_init(NULL);
}

/* ---------- busy rejection ---------- */

void test_enrollment_sm_start_while_active(void) {
  sdf_enrollment_sm_t sm;
  sdf_enrollment_sm_init(&sm);
  sdf_enrollment_sm_start(&sm, 1, 1);

  esp_err_t err = sdf_enrollment_sm_start(&sm, 2, 2);
  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, err);
  TEST_ASSERT_EQUAL(SDF_ENROLLMENT_RESULT_BUSY, sm.result);
  /* Original enrollment should remain intact */
  TEST_ASSERT_EQUAL(1, sm.user_id);
}

/* ---------- failure at step 2 ---------- */

void test_enrollment_sm_failure_at_step2(void) {
  sdf_enrollment_sm_t sm;
  sdf_enrollment_sm_init(&sm);
  sdf_enrollment_sm_start(&sm, 1, 1);
  sdf_enrollment_sm_apply_step_result(&sm, SDF_FINGERPRINT_OP_OK);
  TEST_ASSERT_EQUAL(SDF_ENROLLMENT_STATE_STEP_2, sm.state);

  sdf_enrollment_sm_apply_step_result(&sm, SDF_FINGERPRINT_OP_FULL);
  TEST_ASSERT_EQUAL(SDF_ENROLLMENT_STATE_ERROR, sm.state);
  TEST_ASSERT_EQUAL(SDF_ENROLLMENT_RESULT_FULL, sm.result);
}

void test_enrollment_sm_failure_at_step3(void) {
  sdf_enrollment_sm_t sm;
  sdf_enrollment_sm_init(&sm);
  sdf_enrollment_sm_start(&sm, 1, 1);
  sdf_enrollment_sm_apply_step_result(&sm, SDF_FINGERPRINT_OP_OK);
  sdf_enrollment_sm_apply_step_result(&sm, SDF_FINGERPRINT_OP_OK);
  TEST_ASSERT_EQUAL(SDF_ENROLLMENT_STATE_STEP_3, sm.state);

  sdf_enrollment_sm_apply_step_result(&sm, SDF_FINGERPRINT_OP_IO_ERROR);
  TEST_ASSERT_EQUAL(SDF_ENROLLMENT_STATE_ERROR, sm.state);
  TEST_ASSERT_EQUAL(SDF_ENROLLMENT_RESULT_IO_ERROR, sm.result);
}

/* ---------- apply on non-active is no-op ---------- */

void test_enrollment_sm_apply_on_idle(void) {
  sdf_enrollment_sm_t sm;
  sdf_enrollment_sm_init(&sm);
  sdf_enrollment_sm_apply_step_result_ex(&sm, SDF_FINGERPRINT_OP_OK);
  TEST_ASSERT_EQUAL(SDF_ENROLLMENT_STATE_IDLE, sm.state);
}

void test_enrollment_sm_apply_after_success(void) {
  sdf_enrollment_sm_t sm;
  sdf_enrollment_sm_init(&sm);
  sdf_enrollment_sm_start(&sm, 1, 1);
  sdf_enrollment_sm_apply_step_result_ex(&sm, SDF_FINGERPRINT_OP_OK);
  sdf_enrollment_sm_apply_step_result_ex(&sm, SDF_FINGERPRINT_OP_OK);
  sdf_enrollment_sm_apply_step_result_ex(&sm, SDF_FINGERPRINT_OP_OK);
  TEST_ASSERT_EQUAL(SDF_ENROLLMENT_STATE_SUCCESS, sm.state);

  /* Applying again should be a no-op */
  sdf_enrollment_sm_apply_step_result_ex(&sm, SDF_FINGERPRINT_OP_TIMEOUT);
  TEST_ASSERT_EQUAL(SDF_ENROLLMENT_STATE_SUCCESS, sm.state);
}

/* ---------- boundary permission values ---------- */

void test_enrollment_sm_start_permission_boundaries(void) {
  sdf_enrollment_sm_t sm;

  /* permission 1 valid */
  sdf_enrollment_sm_init(&sm);
  TEST_ASSERT_EQUAL(ESP_OK, sdf_enrollment_sm_start(&sm, 1, 1));

  /* permission 3 valid */
  sdf_enrollment_sm_init(&sm);
  TEST_ASSERT_EQUAL(ESP_OK, sdf_enrollment_sm_start(&sm, 1, 3));

  /* permission 0 invalid */
  sdf_enrollment_sm_init(&sm);
  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, sdf_enrollment_sm_start(&sm, 1, 0));

  /* permission 4 invalid */
  sdf_enrollment_sm_init(&sm);
  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, sdf_enrollment_sm_start(&sm, 1, 4));
}

/* ---------- finger_occupied error ---------- */

void test_enrollment_sm_finger_occupied(void) {
  sdf_enrollment_sm_t sm;
  sdf_enrollment_sm_init(&sm);
  sdf_enrollment_sm_start(&sm, 1, 1);

  sdf_enrollment_sm_apply_step_result_ex(&sm, SDF_FINGERPRINT_OP_FINGER_OCCUPIED);
  TEST_ASSERT_EQUAL(SDF_ENROLLMENT_STATE_ERROR, sm.state);
  TEST_ASSERT_EQUAL(SDF_ENROLLMENT_RESULT_FINGER_OCCUPIED, sm.result);
}

/* ---------- ACK_FAIL retry logic ---------- */

void test_enrollment_sm_retry_on_ack_fail_step1(void) {
  sdf_enrollment_sm_t sm;
  sdf_enrollment_sm_init(&sm);
  sdf_enrollment_sm_start(&sm, 1, 1);

  // ACK_FAIL on step 1 should trigger retry
  sdf_enroll_next_t next = sdf_enrollment_sm_apply_step_result_ex(&sm, SDF_FINGERPRINT_OP_FAILED);
  TEST_ASSERT_EQUAL(SDF_ENROLL_ACT_RETRY_STEP, next.action);
  TEST_ASSERT_EQUAL(SDF_FINGERPRINT_ENROLL_STEP_1, next.cmd);
  TEST_ASSERT_EQUAL(1, next.user_id);
  TEST_ASSERT_EQUAL(1, next.permission);
  TEST_ASSERT_EQUAL(1, next.retry_count);
  TEST_ASSERT_TRUE(sdf_enrollment_sm_is_active(&sm));
  TEST_ASSERT_EQUAL(SDF_ENROLLMENT_STATE_STEP_1, sm.state);
}

void test_enrollment_sm_retry_on_ack_fail_step2(void) {
  sdf_enrollment_sm_t sm;
  sdf_enrollment_sm_init(&sm);
  sdf_enrollment_sm_start(&sm, 1, 1);
  sdf_enrollment_sm_apply_step_result_ex(&sm, SDF_FINGERPRINT_OP_OK); // Step 1 OK
  TEST_ASSERT_EQUAL(SDF_ENROLLMENT_STATE_STEP_2, sm.state);

  // ACK_FAIL on step 2 should trigger retry
  sdf_enroll_next_t next = sdf_enrollment_sm_apply_step_result_ex(&sm, SDF_FINGERPRINT_OP_FAILED);
  TEST_ASSERT_EQUAL(SDF_ENROLL_ACT_RETRY_STEP, next.action);
  TEST_ASSERT_EQUAL(SDF_FINGERPRINT_ENROLL_STEP_2, next.cmd);
  TEST_ASSERT_EQUAL(1, next.retry_count);
  TEST_ASSERT_TRUE(sdf_enrollment_sm_is_active(&sm));
  TEST_ASSERT_EQUAL(SDF_ENROLLMENT_STATE_STEP_2, sm.state);
}

void test_enrollment_sm_max_retries_exceeded_step1(void) {
  sdf_enrollment_sm_t sm;
  sdf_enrollment_sm_init(&sm);
  sdf_enrollment_sm_start(&sm, 1, 1);

  // Default max_retries_step1 = 3
  // Retry 1
  sdf_enroll_next_t next = sdf_enrollment_sm_apply_step_result_ex(&sm, SDF_FINGERPRINT_OP_FAILED);
  TEST_ASSERT_EQUAL(SDF_ENROLL_ACT_RETRY_STEP, next.action);
  TEST_ASSERT_EQUAL(1, next.retry_count);

  // Retry 2
  next = sdf_enrollment_sm_apply_step_result_ex(&sm, SDF_FINGERPRINT_OP_FAILED);
  TEST_ASSERT_EQUAL(SDF_ENROLL_ACT_RETRY_STEP, next.action);
  TEST_ASSERT_EQUAL(2, next.retry_count);

  // Retry 3
  next = sdf_enrollment_sm_apply_step_result_ex(&sm, SDF_FINGERPRINT_OP_FAILED);
  TEST_ASSERT_EQUAL(SDF_ENROLL_ACT_RETRY_STEP, next.action);
  TEST_ASSERT_EQUAL(3, next.retry_count);

  // 4th failure should exceed max retries and fail
  next = sdf_enrollment_sm_apply_step_result_ex(&sm, SDF_FINGERPRINT_OP_FAILED);
  TEST_ASSERT_EQUAL(SDF_ENROLL_ACT_FAIL, next.action);
  TEST_ASSERT_EQUAL(SDF_ENROLLMENT_STATE_ERROR, sm.state);
  TEST_ASSERT_FALSE(sdf_enrollment_sm_is_active(&sm));
}

void test_enrollment_sm_ack_fail_step3_fails_immediately(void) {
  sdf_enrollment_sm_t sm;
  sdf_enrollment_sm_init(&sm);
  sdf_enrollment_sm_start(&sm, 1, 1);
  sdf_enrollment_sm_apply_step_result_ex(&sm, SDF_FINGERPRINT_OP_OK); // Step 1
  sdf_enrollment_sm_apply_step_result_ex(&sm, SDF_FINGERPRINT_OP_OK); // Step 2
  TEST_ASSERT_EQUAL(SDF_ENROLLMENT_STATE_STEP_3, sm.state);

  // ACK_FAIL on step 3 should fail immediately (no retry)
  sdf_enroll_next_t next = sdf_enrollment_sm_apply_step_result_ex(&sm, SDF_FINGERPRINT_OP_FAILED);
  TEST_ASSERT_EQUAL(SDF_ENROLL_ACT_FAIL, next.action);
  TEST_ASSERT_EQUAL(SDF_ENROLLMENT_STATE_ERROR, sm.state);
  TEST_ASSERT_FALSE(sdf_enrollment_sm_is_active(&sm));
}

/* ---------- Enhanced API action tests ---------- */

void test_enrollment_sm_enhanced_api_success_sequence(void) {
  sdf_enrollment_sm_t sm;
  sdf_enrollment_sm_init(&sm);
  sdf_enrollment_sm_start(&sm, 5, 2);

  // Step 1 OK -> should return EXECUTE_STEP for step 2
  sdf_enroll_next_t next = sdf_enrollment_sm_apply_step_result_ex(&sm, SDF_FINGERPRINT_OP_OK);
  TEST_ASSERT_EQUAL(SDF_ENROLL_ACT_EXECUTE_STEP, next.action);
  TEST_ASSERT_EQUAL(SDF_FINGERPRINT_ENROLL_STEP_2, next.cmd);
  TEST_ASSERT_EQUAL(5, next.user_id);
  TEST_ASSERT_EQUAL(2, next.permission);

  // Step 2 OK -> should return EXECUTE_STEP for step 3
  next = sdf_enrollment_sm_apply_step_result_ex(&sm, SDF_FINGERPRINT_OP_OK);
  TEST_ASSERT_EQUAL(SDF_ENROLL_ACT_EXECUTE_STEP, next.action);
  TEST_ASSERT_EQUAL(SDF_FINGERPRINT_ENROLL_STEP_3, next.cmd);

  // Step 3 OK -> should return COMPLETE
  next = sdf_enrollment_sm_apply_step_result_ex(&sm, SDF_FINGERPRINT_OP_OK);
  TEST_ASSERT_EQUAL(SDF_ENROLL_ACT_COMPLETE, next.action);
  TEST_ASSERT_EQUAL(5, next.user_id);
  TEST_ASSERT_EQUAL(2, next.permission);
}

void test_enrollment_sm_enhanced_api_failure_at_step2(void) {
  sdf_enrollment_sm_t sm;
  sdf_enrollment_sm_init(&sm);
  sdf_enrollment_sm_start(&sm, 10, 1);
  sdf_enrollment_sm_apply_step_result_ex(&sm, SDF_FINGERPRINT_OP_OK); // Step 1 OK

  // Failure at step 2
  sdf_enroll_next_t next = sdf_enrollment_sm_apply_step_result_ex(&sm, SDF_FINGERPRINT_OP_FULL);
  TEST_ASSERT_EQUAL(SDF_ENROLL_ACT_FAIL, next.action);
  TEST_ASSERT_EQUAL(10, next.user_id);
  TEST_ASSERT_EQUAL(1, next.permission);
  TEST_ASSERT_EQUAL(SDF_ENROLLMENT_STATE_ERROR, sm.state);
  TEST_ASSERT_EQUAL(SDF_ENROLLMENT_RESULT_FULL, sm.result);
}

/* ---------- Custom retry policy ---------- */

void test_enrollment_sm_custom_retry_policy(void) {
  sdf_enrollment_retry_policy_t policy = {
      .max_retries_step1 = 1,
      .max_retries_step2 = 2,
      .max_retries_step3 = 0,
  };

  sdf_enrollment_sm_t sm;
  sdf_enrollment_sm_init_with_policy(&sm, &policy);
  sdf_enrollment_sm_start(&sm, 1, 1);

  // Only 1 retry allowed on step 1
  sdf_enroll_next_t next = sdf_enrollment_sm_apply_step_result_ex(&sm, SDF_FINGERPRINT_OP_FAILED);
  TEST_ASSERT_EQUAL(SDF_ENROLL_ACT_RETRY_STEP, next.action);
  TEST_ASSERT_EQUAL(1, next.retry_count);

  // 2nd failure should fail
  next = sdf_enrollment_sm_apply_step_result_ex(&sm, SDF_FINGERPRINT_OP_FAILED);
  TEST_ASSERT_EQUAL(SDF_ENROLL_ACT_FAIL, next.action);
}

void test_enrollment_sm_default_retry_policy(void) {
  sdf_enrollment_sm_t sm;
  sdf_enrollment_sm_init(&sm); // Default policy
  TEST_ASSERT_EQUAL(3, sm.retry_policy.max_retries_step1);
  TEST_ASSERT_EQUAL(3, sm.retry_policy.max_retries_step2);
  TEST_ASSERT_EQUAL(0, sm.retry_policy.max_retries_step3);
}

/* ---------- New API getters ---------- */

void test_enrollment_sm_get_state(void) {
  sdf_enrollment_sm_t sm;
  sdf_enrollment_sm_init(&sm);
  TEST_ASSERT_EQUAL(SDF_ENROLLMENT_STATE_IDLE, sdf_enrollment_sm_get_state(&sm));

  sdf_enrollment_sm_start(&sm, 1, 1);
  TEST_ASSERT_EQUAL(SDF_ENROLLMENT_STATE_STEP_1, sdf_enrollment_sm_get_state(&sm));
}

void test_enrollment_sm_get_completed_steps(void) {
  sdf_enrollment_sm_t sm;
  sdf_enrollment_sm_init(&sm);
  TEST_ASSERT_EQUAL_UINT8(0, sdf_enrollment_sm_get_completed_steps(&sm));

  sdf_enrollment_sm_start(&sm, 1, 1);
  sdf_enrollment_sm_apply_step_result_ex(&sm, SDF_FINGERPRINT_OP_OK);
  TEST_ASSERT_EQUAL_UINT8(1, sdf_enrollment_sm_get_completed_steps(&sm));

  sdf_enrollment_sm_apply_step_result_ex(&sm, SDF_FINGERPRINT_OP_OK);
  TEST_ASSERT_EQUAL_UINT8(2, sdf_enrollment_sm_get_completed_steps(&sm));
}

/* ---------- Legacy API compatibility ---------- */

void test_enrollment_sm_legacy_api_still_works(void) {
  sdf_enrollment_sm_t sm;
  sdf_enrollment_sm_init(&sm);
  sdf_enrollment_sm_start(&sm, 1, 1);

  // Using legacy API
  sdf_enrollment_sm_apply_step_result(&sm, SDF_FINGERPRINT_OP_OK);
  TEST_ASSERT_EQUAL(SDF_ENROLLMENT_STATE_STEP_2, sm.state);

  sdf_enrollment_sm_apply_step_result(&sm, SDF_FINGERPRINT_OP_OK);
  TEST_ASSERT_EQUAL(SDF_ENROLLMENT_STATE_STEP_3, sm.state);

  sdf_enrollment_sm_apply_step_result(&sm, SDF_FINGERPRINT_OP_OK);
  TEST_ASSERT_EQUAL(SDF_ENROLLMENT_STATE_SUCCESS, sm.state);
  TEST_ASSERT_EQUAL(SDF_ENROLLMENT_RESULT_SUCCESS, sm.result);
}

void test_enrollment_sm_legacy_api_retry_behavior(void) {
  sdf_enrollment_sm_t sm;
  sdf_enrollment_sm_init(&sm);
  sdf_enrollment_sm_start(&sm, 1, 1);

  // Legacy API should still silently retry on ACK_FAIL
  sdf_enrollment_sm_apply_step_result(&sm, SDF_FINGERPRINT_OP_FAILED);
  TEST_ASSERT_TRUE(sdf_enrollment_sm_is_active(&sm));
  TEST_ASSERT_EQUAL(SDF_ENROLLMENT_STATE_STEP_1, sm.state);

  // Advance and test step 2 retry
  sdf_enrollment_sm_apply_step_result(&sm, SDF_FINGERPRINT_OP_OK);
  sdf_enrollment_sm_apply_step_result(&sm, SDF_FINGERPRINT_OP_FAILED);
  TEST_ASSERT_TRUE(sdf_enrollment_sm_is_active(&sm));
  TEST_ASSERT_EQUAL(SDF_ENROLLMENT_STATE_STEP_2, sm.state);
}
