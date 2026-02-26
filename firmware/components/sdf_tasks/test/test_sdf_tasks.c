#include "unity.h"
#include <string.h>

#include "esp_err.h"
#include "esp_sleep.h"
#include "sdf_tasks.h"

// -----------------------------------------------------------------------------
// Exposed private functions for testing (when SDF_TASKS_TESTING is defined)
// -----------------------------------------------------------------------------
extern sdf_power_wake_reason_t
sdf_tasks_map_wakeup_reason(esp_sleep_wakeup_cause_t cause);

// -----------------------------------------------------------------------------
// Unit Tests
// -----------------------------------------------------------------------------

void test_sdf_tasks_wakeup_reason_mapping(void) {
  // Map normal ESP sleep wakeup causes to the internal SDF reasons
  TEST_ASSERT_EQUAL(SDF_POWER_WAKE_REASON_TIMER,
                    sdf_tasks_map_wakeup_reason(ESP_SLEEP_WAKEUP_TIMER));
  TEST_ASSERT_EQUAL(SDF_POWER_WAKE_REASON_FINGERPRINT,
                    sdf_tasks_map_wakeup_reason(ESP_SLEEP_WAKEUP_GPIO));

  // Everything else maps to OTHER
  TEST_ASSERT_EQUAL(SDF_POWER_WAKE_REASON_OTHER,
                    sdf_tasks_map_wakeup_reason(ESP_SLEEP_WAKEUP_EXT0));
  TEST_ASSERT_EQUAL(SDF_POWER_WAKE_REASON_OTHER,
                    sdf_tasks_map_wakeup_reason(ESP_SLEEP_WAKEUP_WIFI));
  TEST_ASSERT_EQUAL(SDF_POWER_WAKE_REASON_OTHER,
                    sdf_tasks_map_wakeup_reason(ESP_SLEEP_WAKEUP_UNDEFINED));
}

void test_sdf_tasks_checkin_clamping(void) {
  // sdf_tasks_set_checkin_interval_ms enforces:
  // MIN: 1000u
  // MAX: 600000u
  // It returns ESP_ERR_INVALID_ARG on out-of-bounds.
  // Since Zigbee may not be fully initialized in our test environment,
  // valid checks might return ESP_ERR_INVALID_STATE (meaning Zigbee rejected it
  // because it's not started). This is acceptable as long as the argument
  // validation itself passes.

  esp_err_t err;

  // Under min (999ms)
  err = sdf_tasks_set_checkin_interval_ms(999);
  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);

  // At min (1000ms) - should pass bounds check
  err = sdf_tasks_set_checkin_interval_ms(1000);
  TEST_ASSERT_TRUE(err == ESP_OK || err == ESP_ERR_INVALID_STATE);

  // Middle (30000ms)
  err = sdf_tasks_set_checkin_interval_ms(30000);
  TEST_ASSERT_TRUE(err == ESP_OK || err == ESP_ERR_INVALID_STATE);

  // At max (600000ms) - should pass bounds check
  err = sdf_tasks_set_checkin_interval_ms(600000);
  TEST_ASSERT_TRUE(err == ESP_OK || err == ESP_ERR_INVALID_STATE);

  // Over max (600001ms) - should fail bounds check
  err = sdf_tasks_set_checkin_interval_ms(600001);
  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);
}

void test_sdf_tasks_battery_bounds(void) {
  // sdf_tasks_set_battery_percent enforces 0-100 range.
  esp_err_t err;

  // Valid values
  err = sdf_tasks_set_battery_percent(0);
  TEST_ASSERT_TRUE(err == ESP_OK || err == ESP_ERR_INVALID_STATE);

  err = sdf_tasks_set_battery_percent(50);
  TEST_ASSERT_TRUE(err == ESP_OK || err == ESP_ERR_INVALID_STATE);

  err = sdf_tasks_set_battery_percent(100);
  TEST_ASSERT_TRUE(err == ESP_OK || err == ESP_ERR_INVALID_STATE);

  // Invalid values
  err = sdf_tasks_set_battery_percent(101);
  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);

  err = sdf_tasks_set_battery_percent(255);
  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);
}
