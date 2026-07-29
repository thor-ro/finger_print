## Why

System configuration is scattered across 8 Kconfig files, hardcoded defaults in C source, and direct `CONFIG_SDF_*` reads that bypass the runtime config struct. This makes it difficult to find, understand, or modify any single setting — especially GPIO mappings. New contributors must check 10+ files to see all available options. There are also 5 GPIO/pin settings that have no Kconfig entry at all (fp_tx_pin, fp_rx_pin, fp_uart_port, fp_baud_rate, battery_adc_pin) and are hidden as hardcoded defaults in `sdf_config.c`.

## What Changes

- **Consolidate all Kconfig entries** into a single `sdf_config/Kconfig` file, replacing the 8 scattered per-component Kconfig files
- **Add Kconfig entries for all hardcoded GPIO/pin values** currently missing from configuration:
  - `CONFIG_SDF_FP_TX_PIN` (default: 0)
  - `CONFIG_SDF_FP_RX_PIN` (default: 1)
  - `CONFIG_SDF_FP_UART_PORT` (default: 1)
  - `CONFIG_SDF_FP_BAUD_RATE` (default: 19200)
  - `CONFIG_SDF_BATTERY_ADC_PIN` (default: 5)
  - `CONFIG_SDF_FP_RESPONSE_TIMEOUT_MS` (default: 12000)
  - `CONFIG_SDF_FP_RX_BUFFER_SIZE` (default: 256)
  - `CONFIG_SDF_FP_TX_BUFFER_SIZE` (default: 256)
  - `CONFIG_SDF_MATCH_POLL_INTERVAL_MS` (default: 400)
  - `CONFIG_SDF_MATCH_COOLDOWN_MS` (default: 3000)
- **Update `sdf_config.c`** to pull ALL defaults from Kconfig (`CONFIG_SDF_*`) instead of hardcoded literals
- **Route direct `CONFIG_SDF_*` reads through `sdf_config_t`** in components that currently bypass the runtime config (sdf_ota, sdf_storage, sdf_cli, sdf_protocol_zigbee, sdf_platform_sleep, sdf_event_router) where runtime config is appropriate
- **Keep `sdkconfig.defaults`** as the single file for all default values
- **Keep `sdf_config.h`** as the canonical runtime struct definition

## Capabilities

### New Capabilities
- `consolidated-config`: Single source of truth for all system configuration. All GPIO mappings, pin assignments, and system settings are defined in Kconfig with defaults in `sdkconfig.defaults`, and consumed via the `sdf_config_t` runtime struct.

### Modified Capabilities
- no existing specs modified

## Impact

- `firmware/components/sdf_config/` — Kconfig replaces per-component Kconfig files; sdf_config.c pulls all defaults from Kconfig
- `firmware/components/sdf_platform/` — Kconfig removed (moved to sdf_config)
- `firmware/components/sdf_power/` — Kconfig removed (moved to sdf_config)
- `firmware/components/sdf_common/` — Kconfig removed (moved to sdf_config)
- `firmware/components/sdf_ota/` — Kconfig removed (moved to sdf_config); direct `#if CONFIG_SDF_*` reads updated to use sdf_config_t
- `firmware/components/sdf_cli/` — Kconfig removed (moved to sdf_config)
- `firmware/components/sdf_protocol_ble/` — Kconfig removed (moved to sdf_config)
- `firmware/components/sdf_event_router/` — Kconfig removed (moved to sdf_config)
- `firmware/components/sdf_protocol_zigbee/` — Kconfig removed (moved to sdf_config)
- `firmware/sdkconfig.defaults` — unchanged (already all defaults are here)
- Direct Kconfig reads in 6 component source files updated to use `sdf_config_get()`