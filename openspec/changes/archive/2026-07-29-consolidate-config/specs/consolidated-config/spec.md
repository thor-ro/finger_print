## ADDED Requirements

### Requirement: All GPIO/pin mappings defined in Kconfig
Every GPIO and pin assignment (fp_tx_pin, fp_rx_pin, fp_uart_port, fp_baud_rate, fp_power_en_pin, fp_wake_gpio, fp_en_gpio, ws2812_led_gpio, battery_adc_pin, enrollment_btn_gpio) SHALL have a corresponding Kconfig option. No pin value SHALL be hardcoded in C source without a Kconfig counterpart.

#### Scenario: All 10 pin mappings are Kconfig-defined
- **WHEN** a developer runs `idf.py menuconfig`
- **THEN** they can see and modify all GPIO/pin assignments under a single configuration menu

#### Scenario: fp_tx_pin is configurable via Kconfig
- **WHEN** `CONFIG_SDF_FP_TX_PIN` is set to a new value in menuconfig
- **THEN** the fingerprint driver uses that GPIO for UART TX at runtime

#### Scenario: battery_adc_pin is configurable via Kconfig
- **WHEN** `CONFIG_SDF_BATTERY_ADC_PIN` is set to a new value in menuconfig
- **THEN** the battery driver ADC reads from that GPIO at runtime

### Requirement: sdf_config_t is the single runtime config source
All components SHALL read configuration values through `sdf_config_get()` rather than directly reading `CONFIG_SDF_*` Kconfig macros at runtime. Compile-time feature gates (`#if CONFIG_SDF_OTA_SIGNATURE_VERIFY`) are exempt and MUST remain as Kconfig directives.

#### Scenario: Power manager reads checkin interval via sdf_config
- **WHEN** the power manager task needs the check-in interval
- **THEN** it calls `sdf_config_get()->checkin_interval_ms` instead of reading `CONFIG_SDF_POWER_CHECKIN_INTERVAL_MS` directly

#### Scenario: Zigbee protocol reads sleep threshold via sdf_config
- **WHEN** the Zigbee stack needs the sleep threshold
- **THEN** it calls `sdf_config_get()->zigbee_sleep_threshold_ms` instead of reading `CONFIG_SDF_ZIGBEE_SLEEP_THRESHOLD_MS` directly

#### Scenario: OTA signature verify remains compile-time
- **WHEN** the OTA system checks whether signature verification is enabled
- **THEN** it uses `#if CONFIG_SDF_OTA_SIGNATURE_VERIFY` (compile-time gate, not runtime)

### Requirement: sdf_config.c pulls all defaults from Kconfig
The `sdf_config_get_defaults()` function SHALL initialize every field from a `CONFIG_SDF_*` Kconfig option. No field SHALL have a hardcoded fallback literal that bypasses Kconfig.

#### Scenario: fp_tx_pin default comes from Kconfig
- **WHEN** `sdf_config_get_defaults()` is called
- **THEN** `config->fp_tx_pin` is initialized from `CONFIG_SDF_FP_TX_PIN` (not a hardcoded `0`)

#### Scenario: Match poll interval default comes from Kconfig
- **WHEN** `sdf_config_get_defaults()` is called
- **THEN** `config->match_poll_interval_ms` is initialized from `CONFIG_SDF_MATCH_POLL_INTERVAL_MS` (not a hardcoded `400`)

### Requirement: All Kconfig options visible in single menu
All configuration options SHALL be grouped under the "SDF Config" menu in `menuconfig`, using submenus for logical grouping (Platform, Power, Fingerprint, Security, OTA, BLE, Zigbee, CLI, Event Router).

#### Scenario: Developer finds GPIO settings in one place
- **WHEN** a developer opens `idf.py menuconfig` and navigates to "SDF Config"
- **THEN** they see submenus for all config categories including GPIO/pin assignments

#### Scenario: No orphaned Kconfig outside SDF Config menu
- **WHEN** a developer searches for "SDF_" entries in menuconfig
- **THEN** all options appear under "SDF Config" with no duplicate or orphaned entries