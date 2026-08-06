#include "unity.h"

#include "sdf_config.h"

// -----------------------------------------------------------------------------
// Test Setup & Teardown
// -----------------------------------------------------------------------------

/* sdf_config_get_mutable() is only non-NULL when
 * CONFIG_SDF_CONFIG_ENABLE_RUNTIME_OVERRIDE is enabled (test_runner's
 * sdkconfig.defaults turns it on for this reason - it's off by default in
 * production Kconfig). Every setter under test here goes through that
 * pointer, so without the override enabled every "accept" case below would
 * incorrectly return ESP_ERR_INVALID_STATE regardless of the argument.
 *
 * Setters mutate the single global config instance, so each test resets it
 * back to Kconfig defaults afterwards to avoid leaking state into whichever
 * suite (sdf_event_router, sdf_services, ...) runs next and reads
 * sdf_config_get(). */
static void reset_config_to_defaults(void) {
  sdf_config_t *cfg = sdf_config_get_mutable();
  TEST_ASSERT_NOT_NULL(cfg);
  sdf_config_get_defaults(cfg);
}

// -----------------------------------------------------------------------------
// Unit Tests
// -----------------------------------------------------------------------------

void test_sdf_config_set_checkin_interval_bounds(void) {
  reset_config_to_defaults();

  // At min (1000ms) and max (600000ms) - accepted
  TEST_ASSERT_EQUAL(ESP_OK, sdf_config_set_checkin_interval(1000));
  TEST_ASSERT_EQUAL(1000u, sdf_config_get()->checkin_interval_ms);

  TEST_ASSERT_EQUAL(ESP_OK, sdf_config_set_checkin_interval(600000));
  TEST_ASSERT_EQUAL(600000u, sdf_config_get()->checkin_interval_ms);

  // Just outside the bounds on either side - rejected, prior value untouched
  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, sdf_config_set_checkin_interval(999));
  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, sdf_config_set_checkin_interval(600001));
  TEST_ASSERT_EQUAL(600000u, sdf_config_get()->checkin_interval_ms);

  reset_config_to_defaults();
}

void test_sdf_config_set_failed_attempt_threshold_bounds(void) {
  reset_config_to_defaults();

  TEST_ASSERT_EQUAL(ESP_OK, sdf_config_set_failed_attempt_threshold(3));
  TEST_ASSERT_EQUAL(3u, sdf_config_get()->failed_attempt_threshold);

  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, sdf_config_set_failed_attempt_threshold(0));
  TEST_ASSERT_EQUAL(3u, sdf_config_get()->failed_attempt_threshold);

  reset_config_to_defaults();
}

void test_sdf_config_set_failed_attempt_window_bounds(void) {
  reset_config_to_defaults();

  TEST_ASSERT_EQUAL(ESP_OK, sdf_config_set_failed_attempt_window(30000));
  TEST_ASSERT_EQUAL(30000u, sdf_config_get()->failed_attempt_window_ms);

  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, sdf_config_set_failed_attempt_window(0));
  TEST_ASSERT_EQUAL(30000u, sdf_config_get()->failed_attempt_window_ms);

  reset_config_to_defaults();
}

void test_sdf_config_set_lockout_duration_bounds(void) {
  reset_config_to_defaults();

  TEST_ASSERT_EQUAL(ESP_OK, sdf_config_set_lockout_duration(60000));
  TEST_ASSERT_EQUAL(60000u, sdf_config_get()->lockout_duration_ms);

  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, sdf_config_set_lockout_duration(0));
  TEST_ASSERT_EQUAL(60000u, sdf_config_get()->lockout_duration_ms);

  reset_config_to_defaults();
}

void test_sdf_config_set_battery_default_percent_bounds(void) {
  reset_config_to_defaults();

  // At min (0) and max (100) - accepted
  TEST_ASSERT_EQUAL(ESP_OK, sdf_config_set_battery_default_percent(0));
  TEST_ASSERT_EQUAL(0u, sdf_config_get()->battery_default_percent);

  TEST_ASSERT_EQUAL(ESP_OK, sdf_config_set_battery_default_percent(100));
  TEST_ASSERT_EQUAL(100u, sdf_config_get()->battery_default_percent);

  // Just outside the max - rejected, prior value untouched
  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, sdf_config_set_battery_default_percent(101));
  TEST_ASSERT_EQUAL(100u, sdf_config_get()->battery_default_percent);

  reset_config_to_defaults();
}

/* Note: sdf_config_get_mutable() gates purely on
 * CONFIG_SDF_CONFIG_ENABLE_RUNTIME_OVERRIDE - it never checks whether
 * sdf_config_init() has run. There is therefore no reachable
 * "uninitialized-state returns ESP_ERR_INVALID_STATE" case distinct from
 * the override-disabled case already implied above: test_runner's
 * app_main() also calls sdf_config_init() once, before any RUN_TEST(), so
 * an actual pre-init state is never observable from this suite either. */
