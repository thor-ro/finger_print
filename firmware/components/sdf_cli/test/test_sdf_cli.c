#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdf_cli.h"
#include "unity.h"
#include <string.h>

// Note: To test the actual CLI timeout in a reasonable time, we'd need to mock
// FreeRTOS timers or the time system. For a basic integration test, we can
// verify the state transitions.

void test_sdf_cli_initial_state_is_unauthenticated(void) {
  TEST_ASSERT_FALSE(sdf_cli_is_authenticated());
}

void test_sdf_cli_can_authenticate_and_logout(void) {
  sdf_cli_authenticate();
  TEST_ASSERT_TRUE(sdf_cli_is_authenticated());

  sdf_cli_logout();
  TEST_ASSERT_FALSE(sdf_cli_is_authenticated());
}
