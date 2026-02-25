#include "unity.h"
#include <stdio.h>

/* Enrollment SM tests */
extern void test_enrollment_sm_initialization(void);
extern void test_enrollment_sm_start_valid(void);
extern void test_enrollment_sm_start_invalid(void);
extern void test_enrollment_sm_success_sequence(void);
extern void test_enrollment_sm_failure_handling(void);
extern void test_enrollment_sm_user_occupied(void);

/* Driver utils tests */
extern void test_driver_user_id_validation(void);

void app_main(void) {
  printf("Starting Smart Door Firmware (SDF) Tests...\n");

  UNITY_BEGIN();

  RUN_TEST(test_enrollment_sm_initialization);
  RUN_TEST(test_enrollment_sm_start_valid);
  RUN_TEST(test_enrollment_sm_start_invalid);
  RUN_TEST(test_enrollment_sm_success_sequence);
  RUN_TEST(test_enrollment_sm_failure_handling);
  RUN_TEST(test_enrollment_sm_user_occupied);

  RUN_TEST(test_driver_user_id_validation);

  UNITY_END();
}
