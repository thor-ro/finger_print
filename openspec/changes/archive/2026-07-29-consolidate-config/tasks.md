## 1. Consolidated Kconfig

- [x] 1.1 Create sdf_config/Kconfig with all current Kconfig entries from all 8 component Kconfig files, organized into submenus (Platform, Power, Fingerprint, Security, Match, OTA, BLE, Zigbee, CLI, Event Router, Config)
- [x] 1.2 Add new Kconfig entries for hardcoded values: CONFIG_SDF_FP_TX_PIN, CONFIG_SDF_FP_RX_PIN, CONFIG_SDF_FP_UART_PORT, CONFIG_SDF_FP_BAUD_RATE, CONFIG_SDF_BATTERY_ADC_PIN, CONFIG_SDF_MATCH_POLL_INTERVAL_MS, CONFIG_SDF_MATCH_COOLDOWN_MS, CONFIG_SDF_FP_RESPONSE_TIMEOUT_MS, CONFIG_SDF_FP_RX_BUFFER_SIZE, CONFIG_SDF_FP_TX_BUFFER_SIZE
- [x] 1.3 Remove or empty per-component Kconfig files (sdf_platform/Kconfig, sdf_power/Kconfig, sdf_common/Kconfig, sdf_ota/Kconfig, sdf_cli/Kconfig, sdf_protocol_ble/Kconfig, sdf_event_router/Kconfig, sdf_protocol_zigbee/Kconfig)

## 2. Update sdf_config.c Defaults

- [x] 2.1 Replace hardcoded `fp_tx_pin = 0` with `CONFIG_SDF_FP_TX_PIN`
- [x] 2.2 Replace hardcoded `fp_rx_pin = 1` with `CONFIG_SDF_FP_RX_PIN`
- [x] 2.3 Replace hardcoded `fp_uart_port = 1` with `CONFIG_SDF_FP_UART_PORT`
- [x] 2.4 Replace hardcoded `fp_baud_rate = 19200` with `CONFIG_SDF_FP_BAUD_RATE`
- [x] 2.5 Replace hardcoded `battery_adc_pin = 5` with `CONFIG_SDF_BATTERY_ADC_PIN`
- [x] 2.6 Replace hardcoded `match_poll_interval_ms = 400` with `CONFIG_SDF_MATCH_POLL_INTERVAL_MS`
- [x] 2.7 Replace hardcoded `match_cooldown_ms = 3000` with `CONFIG_SDF_MATCH_COOLDOWN_MS`
- [x] 2.8 Replace hardcoded `fp_response_timeout_ms = 12000` with `CONFIG_SDF_FP_RESPONSE_TIMEOUT_MS`
- [x] 2.9 Replace hardcoded `fp_rx_buffer_size = 256` with `CONFIG_SDF_FP_RX_BUFFER_SIZE`
- [x] 2.10 Replace hardcoded `fp_tx_buffer_size = 256` with `CONFIG_SDF_FP_TX_BUFFER_SIZE`

## 3. Update sdkconfig.defaults

- [x] 3.1 Add default values for all new CONFIG_SDF_* entries to firmware/sdkconfig.defaults
- [x] 3.2 Remove duplicate CONFIG_SDF_POWER_FP_EN_GPIO entry (appears twice in sdkconfig.defaults)

## 4. Route Direct Kconfig Reads Through sdf_config_t

- [x] 4.1 Update sdf_protocol_zigbee.c to read zigbee config from sdf_config_t
- [x] 4.2 Update sdf_platform_sleep.c to read retention size from sdf_config_t
- [x] 4.3 Update sdf_event_router.c to read queue depth from sdf_config_t
- [x] 4.4 Keep compile-time #if guards for OTA (sdf_ota.c, sdf_ota_signature.c) and CLI password (sdf_cli.c) — these are correct Kconfig #if usage

## 5. Verification

- [x] 5.1 Run `idf.py build` and verify successful compilation
- [x] 5.2 Run `idf.py menuconfig` and verify all config options appear under SDF Config menu
- [x] 5.3 Verify no hardcoded GPIO/pin values remain in sdf_config.c (grep for numeric pin assignments)
- [x] 5.4 Verify no component reads CONFIG_SDF_POWER_CHECKIN_INTERVAL_MS or similar runtime values directly