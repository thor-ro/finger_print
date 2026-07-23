#include "unity.h"

#include "sdf_services.h"

void test_sdf_services_reset_state_returns_ok(void) {
  esp_err_t err = sdf_services_reset_state();
  TEST_ASSERT_EQUAL(ESP_OK, err);
}

void test_sdf_services_reset_state_can_be_called_multiple_times(void) {
  TEST_ASSERT_EQUAL(ESP_OK, sdf_services_reset_state());
  TEST_ASSERT_EQUAL(ESP_OK, sdf_services_reset_state());
  TEST_ASSERT_EQUAL(ESP_OK, sdf_services_reset_state());
}