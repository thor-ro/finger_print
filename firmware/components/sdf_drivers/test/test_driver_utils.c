#include "sdf_drivers.h"
#include "unity.h"

void test_driver_user_id_validation(void) {
  // Verify the limits are what we expect.
  TEST_ASSERT_EQUAL_UINT16(1, SDF_FINGERPRINT_USER_ID_MIN);
  TEST_ASSERT_EQUAL_UINT16(0x0FFF, SDF_FINGERPRINT_USER_ID_MAX);
}

// More mockable unit tests for the UART packet generation could go here
// if we expose the assembly functions.
