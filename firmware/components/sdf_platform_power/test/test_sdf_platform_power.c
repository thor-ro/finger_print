#include "unity.h"

#include "sdf_platform_power.h"

// -----------------------------------------------------------------------------
// Wake/gate wrapper delegation
// -----------------------------------------------------------------------------

void test_sdf_platform_power_enable_gpio_wake_delegates(void) {
  // Thin wrapper over sdf_platform_sleep_enable_gpio_wakeup_light(), which
  // is a linux no-op (ESP_OK) under CONFIG_IDF_TARGET_LINUX.
  TEST_ASSERT_EQUAL(ESP_OK, sdf_platform_power_enable_gpio_wake(4, 0));
  TEST_ASSERT_EQUAL(ESP_OK, sdf_platform_power_enable_gpio_wake(4, 1));
}

/* Explicitly not called from this suite: sdf_platform_power_enable_timer_wake()
 * and sdf_platform_power_disable_all_wake() delegate straight to
 * sdf_platform_sleep_enable_timer_wakeup()/_disable_all_wakeup_sources(),
 * both unconditional, linux-unguarded passthroughs to the real
 * esp_sleep_enable_timer_wakeup()/esp_sleep_disable_wakeup_source(). Those
 * symbols aren't compiled into esp_hw_support's linux-target build
 * (port/linux/ only has esp_random.c + chip_info.c), so referencing them
 * from any reachable code path is a link failure on IDF_TARGET=linux -
 * confirmed empirically via a real build, not assumed. Same category as
 * sleep_light()/sleep_deep() below; see design.md Non-Goals. */

void test_sdf_platform_power_gate_ble_radio_always_invalid_state(void) {
  // sdf_platform_power_gate_ble_radio() unconditionally returns
  // ESP_ERR_INVALID_STATE for both enable=true and enable=false - this
  // reads as an unimplemented stub, not an intentional contract (see
  // design.md's Decisions). Pinned here so a future change to this
  // behavior is a deliberate, visible diff rather than a silent
  // regression/fix.
  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, sdf_platform_power_gate_ble_radio(true));
  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, sdf_platform_power_gate_ble_radio(false));
}

/* Explicitly not called from this suite: sdf_platform_power_enter_light()
 * and sdf_platform_power_enter_deep() delegate straight to
 * sdf_platform_sleep_light()/_deep(), which hit real esp-idf sleep entry
 * points with no linux guard - see design.md's Non-Goals. */
