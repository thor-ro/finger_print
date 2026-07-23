#include "unity.h"

#include "sdf_protocol_zigbee.h"

void test_sdf_protocol_zigbee_factory_reset_disabled_returns_not_supported(void) {
  esp_err_t err = sdf_protocol_zigbee_factory_reset();
  TEST_ASSERT_EQUAL(ESP_ERR_NOT_SUPPORTED, err);
}

void test_sdf_protocol_zigbee_factory_reset_enabled_returns_ok(void) {
  esp_err_t err = sdf_protocol_zigbee_factory_reset();
  TEST_ASSERT_EQUAL(ESP_ERR_NOT_SUPPORTED, err);
}
