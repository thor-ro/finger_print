#include "unity.h"

#include "sdf_platform_gpio.h"
#include "sdf_platform_nvs.h"
#include "sdf_platform_sleep.h"

// -----------------------------------------------------------------------------
// GPIO
// -----------------------------------------------------------------------------

void test_sdf_platform_gpio_set_level_returns_mock_ok(void) {
  // sdf_common's linux mock (gpio_set_level in sdf_mock_linux_gpio.c) always
  // returns ESP_OK regardless of pin/level.
  TEST_ASSERT_EQUAL(ESP_OK, sdf_platform_gpio_set_level(2, 0));
  TEST_ASSERT_EQUAL(ESP_OK, sdf_platform_gpio_set_level(2, 1));
}

void test_sdf_platform_gpio_get_level_returns_mock_fixed_value(void) {
  // The linux mock's gpio_get_level always returns 1 regardless of pin.
  TEST_ASSERT_EQUAL(1, sdf_platform_gpio_get_level(0));
  TEST_ASSERT_EQUAL(1, sdf_platform_gpio_get_level(3));
}

void test_sdf_platform_gpio_is_rtc_capable_boundary(void) {
  // On ESP32-C6, GPIOs 0-7 are RTC-capable.
  for (int gpio = 0; gpio <= 7; gpio++) {
    TEST_ASSERT_TRUE_MESSAGE(sdf_platform_gpio_is_rtc_capable(gpio), "GPIO 0-7 should be RTC-capable");
  }
  TEST_ASSERT_FALSE(sdf_platform_gpio_is_rtc_capable(8));
  TEST_ASSERT_FALSE(sdf_platform_gpio_is_rtc_capable(9));
}

// -----------------------------------------------------------------------------
// Sleep - pure/deterministic
// -----------------------------------------------------------------------------

void test_sdf_platform_map_wakeup_reason_all_causes(void) {
  TEST_ASSERT_EQUAL(SDF_PLATFORM_WAKE_REASON_TIMER,
                     sdf_platform_map_wakeup_reason(ESP_SLEEP_WAKEUP_TIMER));
  TEST_ASSERT_EQUAL(SDF_PLATFORM_WAKE_REASON_GPIO,
                     sdf_platform_map_wakeup_reason(ESP_SLEEP_WAKEUP_GPIO));

  // Every other esp_sleep_wakeup_cause_t value - explicit cases
  // (UNDEFINED/ALL) and everything falling through `default` - maps to
  // OTHER. (This esp-idf version doesn't define ESP_SLEEP_WAKEUP_USB, so
  // that branch of the switch isn't compiled here.)
  static const esp_sleep_wakeup_cause_t other_causes[] = {
      ESP_SLEEP_WAKEUP_UNDEFINED,       ESP_SLEEP_WAKEUP_ALL,
      ESP_SLEEP_WAKEUP_EXT0,            ESP_SLEEP_WAKEUP_EXT1,
      ESP_SLEEP_WAKEUP_TOUCHPAD,        ESP_SLEEP_WAKEUP_ULP,
      ESP_SLEEP_WAKEUP_UART,            ESP_SLEEP_WAKEUP_UART1,
      ESP_SLEEP_WAKEUP_UART2,           ESP_SLEEP_WAKEUP_WIFI,
      ESP_SLEEP_WAKEUP_COCPU,           ESP_SLEEP_WAKEUP_COCPU_TRAP_TRIG,
      ESP_SLEEP_WAKEUP_BT,              ESP_SLEEP_WAKEUP_VAD,
      ESP_SLEEP_WAKEUP_VBAT_UNDER_VOLT,
  };
  for (size_t i = 0; i < sizeof(other_causes) / sizeof(other_causes[0]); i++) {
    TEST_ASSERT_EQUAL(SDF_PLATFORM_WAKE_REASON_OTHER,
                       sdf_platform_map_wakeup_reason(other_causes[i]));
  }
}

void test_sdf_power_crc16_ccitt_known_vector(void) {
  // CRC-16/CCITT-FALSE (init 0xFFFF, poly 0x1021, no reflect, no xorout)
  // check value for the standard ASCII "123456789" test vector.
  uint16_t crc = 0;
  TEST_ASSERT_EQUAL(ESP_OK, sdf_power_crc16_ccitt("123456789", 9, &crc));
  TEST_ASSERT_EQUAL_HEX16(0x29B1, crc);

  // Empty input never touches the init value.
  TEST_ASSERT_EQUAL(ESP_OK, sdf_power_crc16_ccitt("", 0, &crc));
  TEST_ASSERT_EQUAL_HEX16(0xFFFF, crc);
}

/* Not tested at all: sdf_platform_sleep_configure_wake_sources(). Its
 * compiled object code contains an unconditional call to
 * sdf_platform_sleep_enable_timer_wakeup() (taken only if
 * SDF_WAKE_SRC_TIMER is set, at *runtime*) which is itself an unguarded
 * passthrough to the real esp_sleep_enable_timer_wakeup() - a symbol
 * esp_hw_support's linux-target build never compiles in (port/linux/ only
 * has esp_random.c + chip_info.c). Because that call exists in
 * configure_wake_sources()'s object code regardless of which branch a given
 * call takes, and the linker resolves symbols per translation unit rather
 * than per taken branch, calling this function with ANY argument - not just
 * SDF_WAKE_SRC_TIMER - pulls in the unresolved symbol and fails the link on
 * IDF_TARGET=linux. Confirmed empirically via a real build, not assumed.
 * Same category as sleep_light()/sleep_deep(); see design.md Non-Goals. */

// -----------------------------------------------------------------------------
// Sleep - linux no-ops
// -----------------------------------------------------------------------------

void test_sdf_platform_sleep_retention_linux_noops(void) {
  // Under CONFIG_IDF_TARGET_LINUX there's no real RTC retention memory:
  // write/read are no-ops that still report success, and the buffer isn't
  // touched.
  uint8_t data[4] = {1, 2, 3, 4};
  TEST_ASSERT_EQUAL(ESP_OK, sdf_platform_sleep_retention_write(data, sizeof(data)));

  uint8_t readback[4] = {0xAA, 0xAA, 0xAA, 0xAA};
  static const uint8_t expected_unchanged[4] = {0xAA, 0xAA, 0xAA, 0xAA};
  TEST_ASSERT_EQUAL(ESP_OK, sdf_platform_sleep_retention_read(readback, sizeof(readback)));
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected_unchanged, readback, 4);

  TEST_ASSERT_FALSE(sdf_platform_sleep_retention_valid());
}

void test_sdf_platform_sleep_wakeup_from_linux_noops(void) {
  gpio_num_t gpio_num = 0;
  TEST_ASSERT_FALSE(sdf_platform_sleep_wakeup_from_gpio(&gpio_num));
  TEST_ASSERT_FALSE(sdf_platform_sleep_wakeup_from_timer());
  TEST_ASSERT_FALSE(sdf_platform_sleep_wakeup_from_usb());
}

// -----------------------------------------------------------------------------
// NVS
// -----------------------------------------------------------------------------

void test_sdf_platform_nvs_security_status_defaults_and_erase_before_init(void) {
  // Deliberately does NOT call sdf_platform_nvs_init() - see the comment
  // below. Exercises the two entry points that don't require it instead:
  // get_security_status() against its zero-initialized default, and
  // security_ok()/erase_all()'s pre-init behavior.
  sdf_platform_nvs_security_status_t status = {0};
  TEST_ASSERT_EQUAL(ESP_OK, sdf_platform_nvs_get_security_status(&status));
  TEST_ASSERT_FALSE(status.require_encrypted_nvs);
  TEST_ASSERT_FALSE(status.nvs_encryption_enabled);
  TEST_ASSERT_FALSE(status.nvs_keys_partition_present);
  TEST_ASSERT_FALSE(status.nvs_keys_accessible);

  // require_encrypted_nvs defaults false -> security policy considered
  // satisfied even though nothing has been initialized yet.
  TEST_ASSERT_TRUE(sdf_platform_nvs_security_ok());

  // erase_all() before any init rejects with ESP_ERR_INVALID_STATE.
  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, sdf_platform_nvs_erase_all());
}

/* Explicitly not called from this suite: sdf_platform_sleep_light() and
 * sdf_platform_sleep_deep() hit real esp_light_sleep_start()/
 * esp_deep_sleep_start() with no linux guard - see design.md's Non-Goals.
 * Calling either here risks hanging or terminating the test binary.
 *
 * Also not called: sdf_platform_nvs_init(). Its object code unconditionally
 * references nvs_flash_read_security_cfg()/nvs_flash_generate_keys() inside
 * an `if (keys_partition != NULL)` branch that's dead at runtime on linux
 * (no NVS keys partition exists) but not dead at *link* time - the linker
 * still has to resolve those symbols because the function is reachable.
 * They live in the `nvs_sec_provider` component, which nvs_flash's own
 * CMakeLists.txt deliberately omits from its linux-target REQUIRES
 * ("mbedtls isn't configured for building with linux ... it will draw in
 * all kind of dependencies") - so this isn't a gap in sdf_platform, it's
 * the same upstream linux-target limitation as the sleep entry points
 * above, discovered empirically via a real build. See design.md Non-Goals. */
