## 1. sdf_config

- [x] 1.1 Create `firmware/components/sdf_config/test/test_sdf_config.c`
- [x] 1.2 Cover `sdf_config_set_checkin_interval`: accept `1000`, accept `600000`, reject `999`, reject `600001`
- [x] 1.3 Cover `sdf_config_set_failed_attempt_threshold` / `sdf_config_set_failed_attempt_window` / `sdf_config_set_lockout_duration`: accept a valid value, reject `0`
- [x] 1.4 Cover `sdf_config_set_battery_default_percent`: accept `0`, accept `100`, reject `101`
- [x] 1.5 Not reachable: `sdf_config_get_mutable()` gates purely on `CONFIG_SDF_CONFIG_ENABLE_RUNTIME_OVERRIDE` (now enabled in `test_runner/sdkconfig.defaults` so 1.2-1.4 can exercise real validation), not on init state, and `test_runner_main.c` already calls `sdf_config_init()` before any `RUN_TEST()`. Documented in a comment in `test_sdf_config.c` instead of an untestable case.

## 2. sdf_platform

- [x] 2.1 Create `firmware/components/sdf_platform/test/test_sdf_platform.c`
- [x] 2.2 GPIO: `sdf_platform_gpio_set_level`/`_get_level` against the linux mock's fixed behavior (`sdf_mock_linux_gpio.c`); `sdf_platform_gpio_is_rtc_capable()` true for GPIO 0-7, false for GPIO 8+
- [x] 2.3 Sleep (pure): `sdf_platform_map_wakeup_reason()` for every `esp_sleep_wakeup_cause_t` case; `sdf_power_crc16_ccitt()` against a known test vector. **Deviation (discovered via a real build):** `sdf_platform_sleep_configure_wake_sources()` is NOT called at all, with any argument - its compiled object code unconditionally references `esp_sleep_enable_timer_wakeup` (only reachable on the `SDF_WAKE_SRC_TIMER` branch at *runtime*, but referenced regardless at *link* time), which `esp_hw_support`'s `IDF_TARGET=linux` build never compiles in. This fails the link, not just a runtime risk - see design.md Non-Goals.
- [x] 2.4 Sleep (linux no-ops): `sdf_platform_sleep_retention_write/read/valid()` return their documented linux no-op values (`ESP_OK`/`ESP_OK`/`false`); `sdf_platform_sleep_wakeup_from_gpio/_timer/_usb()` all return `false`
- [x] 2.5 NVS. **Deviation (discovered via a real build):** `sdf_platform_nvs_init()` is NOT called - its object code unconditionally references `nvs_flash_read_security_cfg`/`nvs_flash_generate_keys`, which live in `nvs_sec_provider`, a component `nvs_flash`'s own `CMakeLists.txt` deliberately excludes from its linux-target `REQUIRES` (upstream limitation, not a gap here). Covered instead: `_get_security_status()`'s zero-initialized default, `_security_ok()`'s true-when-not-required-and-not-initialized default, and `_erase_all()`'s pre-init `ESP_ERR_INVALID_STATE`. See design.md Non-Goals.
- [x] 2.6 Explicitly do NOT call `sdf_platform_sleep_light()`/`sdf_platform_sleep_deep()` (see design.md Non-Goals)

## 3. sdf_platform_power

- [x] 3.1 Create `firmware/components/sdf_platform_power/test/test_sdf_platform_power.c`
- [x] 3.2 `sdf_platform_power_enable_gpio_wake()` delegates and returns the underlying `sdf_platform_sleep_*` result. **Deviation (discovered via a real build):** `_enable_timer_wake()`/`_disable_all_wake()` are NOT called - both delegate to `sdf_platform_sleep_*` functions that fail to link on `IDF_TARGET=linux` (same root cause as 2.3's `configure_wake_sources()`). See design.md Non-Goals.
- [x] 3.3 `sdf_platform_power_gate_ble_radio()` pinned as always returning `ESP_ERR_INVALID_STATE` for both `enable=true` and `enable=false`, with a comment noting this looks like an unimplemented stub, not an intentional contract
- [x] 3.4 Explicitly do NOT call `sdf_platform_power_enter_light()`/`_enter_deep()` (see design.md Non-Goals)

## 4. sdf_storage web-user coverage

- [x] 4.1 Confirm `sdf_storage_web_user_save()`'s actual index/overflow behavior by reading `sdf_storage.c` (it takes an explicit `index < SDF_STORAGE_WEB_USER_MAX`, not an auto-allocated slot — confirm before writing the boundary test)
- [x] 4.2 Add to `firmware/components/sdf_storage/test/test_sdf_storage.c`: save/load round-trip, load-not-found, `find_by_name` hit/miss, `clear`, `clear_all`, `count`, and `index == SDF_STORAGE_WEB_USER_MAX` rejected with `ESP_ERR_INVALID_ARG`

## 5. Wiring and validation

- [x] 5.1 Add `test_sdf_config.c`, `test_sdf_platform.c`, `test_sdf_platform_power.c` to `firmware/test_runner/main/CMakeLists.txt`'s `SRCS` (`test_sdf_storage.c` is already listed)
- [x] 5.2 Add `extern`/`RUN_TEST()` entries for every new test function to `firmware/test_runner/main/test_runner_main.c`
- [x] 5.3 Run `cd firmware/test_runner && idf.py build && ./build/sdf_test_runner.elf`, confirm all new and existing tests pass and the process exits promptly (no hang from the excluded sleep-entry functions). 156 Tests, 0 Failures, 11 Ignored, exit code 0. Two build-time deviations were needed beyond the tasks above: (1) `sdf_power_crc16_ccitt()` was defined but never declared in any header - added its declaration to `sdf_platform_sleep.h`; (2) a first build attempt that DID call `sdf_platform_sleep_configure_wake_sources()`, `sdf_platform_power_enable_timer_wake()`/`_disable_all_wake()`, and `sdf_platform_nvs_init()` failed at the link step with `Undefined symbols for architecture arm64` for `esp_sleep_enable_timer_wakeup`/`esp_sleep_disable_wakeup_source`/`nvs_flash_generate_keys`/`nvs_flash_read_security_cfg` - these calls were then removed per the updated Non-Goals (see design.md and tasks 2.3/2.5/3.2 above).
- [x] 5.4 Run `openspec validate add-linux-host-tests-config-platform --strict`
