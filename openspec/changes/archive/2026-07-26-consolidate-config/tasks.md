## 1. Extend sdf_config (Additive Phase)

### 1.1 Extend sdf_config_t struct
- [ ] 1.1.1 Add Power Management fields (12) to `sdf_config.h`
- [ ] 1.1.2 Add Fingerprint Services fields (11) to `sdf_config.h`
- [ ] 1.1.3 Add Zigbee fields (3) to `sdf_config.h`
- [ ] 1.1.4 Add Nuki BLE fields (4) to `sdf_config.h`
- [ ] 1.1.5 Add Security fields (3) to `sdf_config.h`
- [ ] 1.1.6 Add System fields (2) to `sdf_config.h`
- [ ] 1.1.7 Add comment section headers for each domain

### 1.2 Implement get_defaults mapping
- [ ] 1.2.1 Map Power Kconfig → struct fields in `sdf_config_get_defaults()`
- [ ] 1.2.2 Map Services Kconfig → struct fields
- [ ] 1.2.3 Map Zigbee Kconfig → struct fields
- [ ] 1.2.4 Map Nuki BLE Kconfig → struct fields (new Kconfig entries)
- [ ] 1.2.5 Map Security Kconfig → struct fields
- [ ] 1.2.6 Map System Kconfig → struct fields
- [ ] 1.2.7 Handle missing Kconfig with compile-time defaults

### 1.3 Extend validation
- [ ] 1.3.1 Add range validation for all numeric fields
- [ ] 1.3.2 Add cross-field validation (idle > wake_guard, etc.)
- [ ] 1.3.3 Add Zigbee enable check against CONFIG_ZB_ENABLED
- [ ] 1.3.4 Add Nuki BLE address validation (non-zero if connect_on_demand)
- [ ] 1.3.5 Update `sdf_config_dump()` to print all domains

### 1.4 Add setter functions
- [ ] 1.4.1 Implement `sdf_config_set_checkin_interval()`
- [ ] 1.4.2 Implement `sdf_config_set_match_poll_interval()`
- [ ] 1.4.3 Implement `sdf_config_set_zigbee_enabled()`
- [ ] 1.4.4 Implement `sdf_config_set_battery_report_interval()`
- [ ] 1.4.5 Add setters for other critical runtime-configurable fields
- [ ] 1.4.6 Setters call component-specific update functions (e.g., Zigbee checkin)

### 1.5 Unit tests
- [ ] 1.5.1 Test `get_defaults()` produces valid config
- [ ] 1.5.2 Test `validate()` catches out-of-range values
- [ ] 1.5.3 Test `validate()` catches cross-field violations
- [ ] 1.5.4 Test setters update struct and propagate to components
- [ ] 1.5.5 Test `sdf_config_dump()` output format

## 2. Migrate sdf_power

### 2.1 Update initialization
- [ ] 2.1.1 Replace `sdf_power_get_default_power_config()` call with `sdf_config_get()`
- [ ] 2.1.2 Map `sdf_config_t` → `sdf_power_manager_config_t` inline
- [ ] 2.1.3 Remove `sdf_power_get_default_power_config()` function
- [ ] 2.1.4 Update `sdf_power.h` - remove declaration

### 2.2 Test
- [ ] 2.2.1 Verify power task starts with config values
- [ ] 2.2.2 Verify sleep/wake intervals use config
- [ ] 2.2.3 Verify battery reporting interval uses config
- [ ] 2.2.4 Test setter `sdf_config_set_checkin_interval()` updates Zigbee

## 3. Migrate sdf_services

### 3.1 Update initialization
- [ ] 3.1.1 Replace `sdf_services_get_default_config()` call with `sdf_config_get()`
- [ ] 3.1.2 Map `sdf_config_t` → `sdf_services_config_t` inline
- [ ] 3.1.3 Remove `sdf_services_get_default_config()` function
- [ ] 3.1.4 Update `sdf_services.h` - remove declaration

### 3.2 Test
- [ ] 3.2.1 Verify match poll interval uses config
- [ ] 3.2.2 Verify cooldown/lockout thresholds use config
- [ ] 3.2.3 Verify UART pins use config
- [ ] 3.2.4 Test setter `sdf_config_set_match_poll_interval()` updates running task

## 4. Migrate sdf_app

### 4.1 Remove inline Kconfig references
- [ ] 4.1.1 Replace `SDF_APP_POWER_CHECKIN_INTERVAL_MS` with `sdf_config_get()->checkin_interval_ms`
- [ ] 4.1.2 Replace `SDF_APP_POWER_IDLE_BEFORE_SLEEP_MS` with config
- [ ] 4.1.3 Replace `SDF_APP_POWER_POST_WAKE_GUARD_MS` with config
- [ ] 4.1.4 Replace `SDF_APP_POWER_LOOP_INTERVAL_MS` with config
- [ ] 4.1.5 Replace `SDF_APP_POWER_BATTERY_REPORT_MS` with config
- [ ] 4.1.6 Replace `SDF_APP_POWER_BATTERY_DEFAULT_PERCENT` with config
- [ ] 4.1.7 Replace `SDF_APP_POWER_ENABLE_LIGHT_SLEEP` with config
- [ ] 4.1.8 Replace `SDF_APP_POWER_ENABLE_BLE_RADIO_GATING` with config
- [ ] 4.1.9 Replace `SDF_APP_FP_WAKE_GPIO` with `sdf_config_get()->fp_wake_gpio`
- [ ] 4.1.10 Replace `SDF_APP_FP_POWER_EN_GPIO` with `sdf_config_get()->fp_power_en_gpio`
- [ ] 4.1.11 Replace `SDF_APP_ENROLLMENT_BTN_GPIO` with `sdf_config_get()->enrollment_btn_gpio`
- [ ] 4.1.12 Replace `SDF_APP_WS2812_LED_GPIO` with `sdf_config_get()->ws2812_led_gpio`
- [ ] 4.1.13 Replace `SDF_APP_BATTERY_ADC_GPIO` with `sdf_config_get()->battery_adc_pin`
- [ ] 4.1.14 Replace Zigbee enabled check with `sdf_config_get()->zigbee_enabled`
- [ ] 4.1.15 Replace Nuki BLE address with `sdf_config_get()->nuki_target_addr`

### 4.2 Update power config setup
- [ ] 4.2.1 Remove inline `sdf_power_manager_config_t` population
- [ ] 4.2.2 Use mapped config from `sdf_config_get()`

### 4.3 Test
- [ ] 4.3.1 Full integration test: boot → fingerprint match → unlock
- [ ] 4.3.2 Verify Zigbee commands work with config values
- [ ] 4.3.3 Verify Nuki pairing uses config address

## 5. Consolidate Kconfig

### 5.1 Create unified menu structure
- [ ] 5.1.1 Add `menu "SDF Configuration"` in `sdf_config/Kconfig`
- [ ] 5.1.2 Source Power submenu from `sdf_power/Kconfig`
- [ ] 5.1.3 Source Services submenu from `sdf_services/Kconfig`
- [ ] 5.1.4 Source Zigbee submenu from `sdf_protocol_zigbee/Kconfig`
- [ ] 5.1.5 Add Nuki BLE submenu (new Kconfig entries)
- [ ] 5.1.6 Add Security submenu
- [ ] 5.1.7 Add System submenu

### 5.2 Add Nuki BLE Kconfig entries
- [ ] 5.2.1 `CONFIG_SDF_NUKI_TARGET_ADDR_TYPE` (int, 0..1)
- [ ] 5.2.2 `CONFIG_SDF_NUKI_TARGET_ADDR` (hex string, 12 chars)
- [ ] 5.2.3 `CONFIG_SDF_BLE_CONNECT_ON_DEMAND` (bool)

### 5.3 Add missing Kconfig entries
- [ ] 5.3.1 Services UART pins/baud (fp_uart_port, fp_tx_pin, fp_rx_pin, fp_baud_rate)
- [ ] 5.3.2 Power deep sleep fallback enable
- [ ] 5.3.3 Zigbee sleep threshold
- [ ] 5.3.4 Power FP power enable GPIO (already in sdf_power but verify)

### 5.4 Update sdkconfig.defaults
- [ ] 5.4.1 Sync all new Kconfig keys with current defaults
- [ ] 5.4.2 Remove old scattered defaults

## 6. Cleanup

### 6.1 Remove dead code
- [ ] 6.1.1 Delete `sdf_power_get_default_power_config()` implementation
- [ ] 6.1.2 Delete `sdf_services_get_default_config()` implementation
- [ ] 6.1.3 Remove dead includes in components

### 6.2 Update documentation
- [ ] 6.2.1 Update `doc/sdf_sas.md` §8 (Cross-cutting → Configuration)
- [ ] 6.2.2 Update `doc/sdf_sas.md` §15 (Configuration and Provisioning)
- [ ] 6.2.3 Update `AGENTS.md` Component Structure list
- [ ] 6.2.4 Update `software-architecture.md` §7 (Module Decomposition)

## 7. Validation & Integration

### 7.1 Build verification
- [ ] 7.1.1 `idf.py build` succeeds
- [ ] 7.1.2 `idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.debug.defaults" build` succeeds
- [ ] 7.1.3 `idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.release.defaults" build` succeeds

### 7.2 Runtime tests
- [ ] 7.2.1 Flash debug build, verify boot logs show config dump
- [ ] 7.2.2 Test CLI `config dump` command shows all domains
- [ ] 7.2.3 Test CLI `config set checkin_interval 30000` updates runtime
- [ ] 7.2.4 Test CLI `config set match_poll 200` updates running task
- [ ] 7.2.5 Test CLI `config set zigbee_enabled false` disables Zigbee
- [ ] 7.2.6 Full biometric unlock flow works
- [ ] 7.2.7 Full Zigbee remote unlock works
- [ ] 7.2.8 Enrollment flow works
- [ ] 7.2.9 Deep sleep / wake cycle works

### 7.3 Migration test
- [ ] 7.3.1 Build with old `sdkconfig` (if exists), verify backward compatibility
- [ ] 7.3.2 Test `migrate_sdkconfig.py` script (if created)

## 8. Open Questions Resolution

### 8.1 Field naming standardization
- [ ] 8.1.1 DECIDE: Standardize `fp_wake_gpio` / `fp_power_en_gpio` across power/services?
- [ ] 8.1.2 UPDATE: Rename fields if decision = yes

### 8.2 Nuki BLE Kconfig location
- [ ] 8.2.1 DECIDE: `sdf_protocol_ble/Kconfig` or `sdf_config/Kconfig`?
- [ ] 8.2.2 IMPLEMENT: Add Kconfig in chosen location

### 8.3 Thread safety for get_mutable()
- [ ] 8.3.1 DECIDE: Add mutex or document "init only"?
- [ ] 8.3.2 IMPLEMENT: If mutex, add to `sdf_config.c`

### 8.4 Build-time defaults generation
- [ ] 8.4.1 DECIDE: Generate `sdf_config_defaults` struct at CMake time?
- [ ] 8.4.2 IMPLEMENT: If yes, add CMake generator script

### 8.5 Validation on mutation
- [ ] 8.5.1 DECIDE: Add setters for all fields or only critical?
- [ ] 8.5.2 IMPLEMENT: If all, generate setter stubs

### 8.6 Deprecation timeline
- [ ] 8.6.1 DECIDE: How many releases to keep old functions with warnings?
- [ ] 8.6.2 IMPLEMENT: Add `__attribute__((deprecated))` if keeping

### 8.7 Migration script
- [ ] 8.7.1 DECIDE: Create `tools/migrate_sdkconfig.py`?
- [ ] 8.7.2 IMPLEMENT: If yes, auto-generate from Kconfig diff

### 8.8 Documentation sync
- [ ] 8.8.1 VERIFY: `sdf_sas.md` §8, §15 updated
- [ ] 8.8.2 VERIFY: `AGENTS.md` component list updated
- [ ] 8.8.3 VERIFY: `software-architecture.md` §7 updated