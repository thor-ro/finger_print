#include "unity.h"
#include <string.h>

#include "esp_err.h"
#include "esp_sleep.h"
#include "sdf_config.h"
#include "sdf_power.h"

// -----------------------------------------------------------------------------
// Exposed private functions for testing (when SDF_POWER_TESTING is defined)
// -----------------------------------------------------------------------------
extern sdf_power_wake_reason_t
sdf_power_map_wakeup_reason(esp_sleep_wakeup_cause_t cause);
extern void test_sdf_power_set_base_checkin_interval_ms(uint32_t interval_ms);
extern void test_sdf_power_set_battery_percent_raw(uint8_t battery_percent);

// -----------------------------------------------------------------------------
// Unit Tests
// -----------------------------------------------------------------------------

void test_sdf_power_wakeup_reason_mapping(void) {
  // Map normal ESP sleep wakeup causes to the internal SDF reasons
  TEST_ASSERT_EQUAL(SDF_POWER_WAKE_REASON_TIMER,
                    sdf_power_map_wakeup_reason(ESP_SLEEP_WAKEUP_TIMER));
  TEST_ASSERT_EQUAL(SDF_POWER_WAKE_REASON_FINGERPRINT,
                    sdf_power_map_wakeup_reason(ESP_SLEEP_WAKEUP_GPIO));

  // Everything else maps to OTHER
  TEST_ASSERT_EQUAL(SDF_POWER_WAKE_REASON_OTHER,
                    sdf_power_map_wakeup_reason(ESP_SLEEP_WAKEUP_EXT0));
  TEST_ASSERT_EQUAL(SDF_POWER_WAKE_REASON_OTHER,
                    sdf_power_map_wakeup_reason(ESP_SLEEP_WAKEUP_WIFI));
  TEST_ASSERT_EQUAL(SDF_POWER_WAKE_REASON_OTHER,
                    sdf_power_map_wakeup_reason(ESP_SLEEP_WAKEUP_UNDEFINED));
}

void test_sdf_power_checkin_clamping(void) {
  // sdf_power_set_checkin_interval_ms enforces:
  // MIN: 1000u
  // MAX: 600000u
  // It returns ESP_ERR_INVALID_ARG on out-of-bounds.
  // Since Zigbee may not be fully initialized in our test environment,
  // valid checks might return ESP_ERR_INVALID_STATE (meaning Zigbee rejected it
  // because it's not started). This is acceptable as long as the argument
  // validation itself passes.

  esp_err_t err;

  // Under min (999ms)
  err = sdf_power_set_checkin_interval_ms(999);
  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);

  // At min (1000ms) - should pass bounds check
  err = sdf_power_set_checkin_interval_ms(1000);
  TEST_ASSERT_TRUE(err == ESP_OK || err == ESP_ERR_INVALID_STATE);

  // Middle (30000ms)
  err = sdf_power_set_checkin_interval_ms(30000);
  TEST_ASSERT_TRUE(err == ESP_OK || err == ESP_ERR_INVALID_STATE);

  // At max (600000ms) - should pass bounds check
  err = sdf_power_set_checkin_interval_ms(600000);
  TEST_ASSERT_TRUE(err == ESP_OK || err == ESP_ERR_INVALID_STATE);

  // Over max (600001ms) - should fail bounds check
  err = sdf_power_set_checkin_interval_ms(600001);
  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);
}

void test_sdf_power_battery_bounds(void) {
  // sdf_power_set_battery_percent enforces 0-100 range.
  esp_err_t err;

  // Valid values
  err = sdf_power_set_battery_percent(0);
  TEST_ASSERT_TRUE(err == ESP_OK || err == ESP_ERR_INVALID_STATE);

  err = sdf_power_set_battery_percent(50);
  TEST_ASSERT_TRUE(err == ESP_OK || err == ESP_ERR_INVALID_STATE);

  err = sdf_power_set_battery_percent(100);
  TEST_ASSERT_TRUE(err == ESP_OK || err == ESP_ERR_INVALID_STATE);

  // Invalid values
  err = sdf_power_set_battery_percent(101);
  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);

  err = sdf_power_set_battery_percent(255);
  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);
}

void test_sdf_power_calculate_checkin_interval_disabled_returns_base(void) {
  sdf_config_t *cfg = sdf_config_get_mutable();
  TEST_ASSERT_NOT_NULL(cfg);
  bool saved_adaptive = cfg->adaptive_checkin;
  cfg->adaptive_checkin = false;

  test_sdf_power_set_base_checkin_interval_ms(10000);

  /* With adaptive_checkin disabled, the base interval is returned
   * regardless of battery tier. */
  const uint8_t tiers[] = {5, 19, 20, 39, 40, 59, 60, 90};
  for (size_t i = 0; i < sizeof(tiers) / sizeof(tiers[0]); ++i) {
    test_sdf_power_set_battery_percent_raw(tiers[i]);
    TEST_ASSERT_EQUAL_UINT32(10000, sdf_power_calculate_checkin_interval());
  }

  cfg->adaptive_checkin = saved_adaptive;
}

void test_sdf_power_calculate_checkin_interval_enabled_scales_with_battery(
    void) {
  sdf_config_t *cfg = sdf_config_get_mutable();
  TEST_ASSERT_NOT_NULL(cfg);
  bool saved_adaptive = cfg->adaptive_checkin;
  cfg->adaptive_checkin = true;

  test_sdf_power_set_base_checkin_interval_ms(10000);

  /* >= 60%: unscaled base. */
  test_sdf_power_set_battery_percent_raw(80);
  TEST_ASSERT_EQUAL_UINT32(10000, sdf_power_calculate_checkin_interval());

  /* 40-59%: base * 1.5. */
  test_sdf_power_set_battery_percent_raw(50);
  TEST_ASSERT_EQUAL_UINT32(15000, sdf_power_calculate_checkin_interval());

  /* 20-39%: base * 2. */
  test_sdf_power_set_battery_percent_raw(30);
  TEST_ASSERT_EQUAL_UINT32(20000, sdf_power_calculate_checkin_interval());

  /* < 20%: base * 4 - a larger value than the unscaled base. */
  test_sdf_power_set_battery_percent_raw(10);
  TEST_ASSERT_EQUAL_UINT32(40000, sdf_power_calculate_checkin_interval());

  cfg->adaptive_checkin = saved_adaptive;
}
