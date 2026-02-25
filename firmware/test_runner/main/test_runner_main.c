#include "unity.h"
#include <stdio.h>

/* Enrollment SM tests */
extern void test_enrollment_sm_initialization(void);
extern void test_enrollment_sm_start_valid(void);
extern void test_enrollment_sm_start_invalid(void);
extern void test_enrollment_sm_success_sequence(void);
extern void test_enrollment_sm_failure_handling(void);
extern void test_enrollment_sm_user_occupied(void);
extern void test_enrollment_sm_is_active_idle(void);
extern void test_enrollment_sm_is_active_steps(void);
extern void test_enrollment_sm_is_active_error(void);
extern void test_enrollment_sm_is_active_wait_admin(void);
extern void test_enrollment_sm_current_step(void);
extern void test_enrollment_sm_current_command(void);
extern void test_enrollment_sm_reset(void);
extern void test_enrollment_sm_null_safety(void);
extern void test_enrollment_sm_start_while_active(void);
extern void test_enrollment_sm_failure_at_step2(void);
extern void test_enrollment_sm_failure_at_step3(void);
extern void test_enrollment_sm_apply_on_idle(void);
extern void test_enrollment_sm_apply_after_success(void);
extern void test_enrollment_sm_start_permission_boundaries(void);
extern void test_enrollment_sm_finger_occupied(void);

/* Driver utils tests */
extern void test_driver_user_id_validation(void);

/* Driver protocol tests */
extern void test_checksum_all_zeros(void);
extern void test_checksum_known_command(void);
extern void test_checksum_with_parameters(void);
extern void test_checksum_all_ff(void);
extern void test_checksum_cancellation(void);
extern void test_user_id_valid_min(void);
extern void test_user_id_valid_max(void);
extern void test_user_id_valid_mid(void);
extern void test_user_id_invalid_zero(void);
extern void test_user_id_invalid_above_max(void);
extern void test_user_id_invalid_uint16_max(void);
extern void test_map_ack_success(void);
extern void test_map_ack_timeout(void);
extern void test_map_ack_full(void);
extern void test_map_ack_user_occupied(void);
extern void test_map_ack_finger_occupied(void);
extern void test_map_ack_fail(void);
extern void test_map_ack_nouser(void);
extern void test_map_ack_unknown(void);

/* Lock flow tests */
extern void test_lock_flow_init(void);
extern void test_lock_flow_reset_preserves_max_retries(void);
extern void test_lock_flow_is_idle_on_init(void);
extern void test_lock_flow_is_not_idle_when_active(void);
extern void test_lock_flow_start_success(void);
extern void test_lock_flow_start_rejected_when_active(void);
extern void test_lock_flow_state_transitions(void);
extern void test_lock_flow_retries_not_exhausted(void);
extern void test_lock_flow_advance_retry(void);
extern void test_lock_flow_retries_exhausted_at_max(void);
extern void test_lock_flow_zero_max_retries(void);

void app_main(void) {
  printf("Starting Smart Door Firmware (SDF) Tests...\n");

  UNITY_BEGIN();

  /* Enrollment SM */
  RUN_TEST(test_enrollment_sm_initialization);
  RUN_TEST(test_enrollment_sm_start_valid);
  RUN_TEST(test_enrollment_sm_start_invalid);
  RUN_TEST(test_enrollment_sm_success_sequence);
  RUN_TEST(test_enrollment_sm_failure_handling);
  RUN_TEST(test_enrollment_sm_user_occupied);
  RUN_TEST(test_enrollment_sm_is_active_idle);
  RUN_TEST(test_enrollment_sm_is_active_steps);
  RUN_TEST(test_enrollment_sm_is_active_error);
  RUN_TEST(test_enrollment_sm_is_active_wait_admin);
  RUN_TEST(test_enrollment_sm_current_step);
  RUN_TEST(test_enrollment_sm_current_command);
  RUN_TEST(test_enrollment_sm_reset);
  RUN_TEST(test_enrollment_sm_null_safety);
  RUN_TEST(test_enrollment_sm_start_while_active);
  RUN_TEST(test_enrollment_sm_failure_at_step2);
  RUN_TEST(test_enrollment_sm_failure_at_step3);
  RUN_TEST(test_enrollment_sm_apply_on_idle);
  RUN_TEST(test_enrollment_sm_apply_after_success);
  RUN_TEST(test_enrollment_sm_start_permission_boundaries);
  RUN_TEST(test_enrollment_sm_finger_occupied);

  /* Driver utils */
  RUN_TEST(test_driver_user_id_validation);

  /* Driver protocol */
  RUN_TEST(test_checksum_all_zeros);
  RUN_TEST(test_checksum_known_command);
  RUN_TEST(test_checksum_with_parameters);
  RUN_TEST(test_checksum_all_ff);
  RUN_TEST(test_checksum_cancellation);
  RUN_TEST(test_user_id_valid_min);
  RUN_TEST(test_user_id_valid_max);
  RUN_TEST(test_user_id_valid_mid);
  RUN_TEST(test_user_id_invalid_zero);
  RUN_TEST(test_user_id_invalid_above_max);
  RUN_TEST(test_user_id_invalid_uint16_max);
  RUN_TEST(test_map_ack_success);
  RUN_TEST(test_map_ack_timeout);
  RUN_TEST(test_map_ack_full);
  RUN_TEST(test_map_ack_user_occupied);
  RUN_TEST(test_map_ack_finger_occupied);
  RUN_TEST(test_map_ack_fail);
  RUN_TEST(test_map_ack_nouser);
  RUN_TEST(test_map_ack_unknown);

  /* Lock flow */
  RUN_TEST(test_lock_flow_init);
  RUN_TEST(test_lock_flow_reset_preserves_max_retries);
  RUN_TEST(test_lock_flow_is_idle_on_init);
  RUN_TEST(test_lock_flow_is_not_idle_when_active);
  RUN_TEST(test_lock_flow_start_success);
  RUN_TEST(test_lock_flow_start_rejected_when_active);
  RUN_TEST(test_lock_flow_state_transitions);
  RUN_TEST(test_lock_flow_retries_not_exhausted);
  RUN_TEST(test_lock_flow_advance_retry);
  RUN_TEST(test_lock_flow_retries_exhausted_at_max);
  RUN_TEST(test_lock_flow_zero_max_retries);

  UNITY_END();
}
