# AGENTS.md — Smart Door Finger (SDF) v2.0

## Project Overview
ESP-IDF v5.5.3 firmware for ESP32-C6 biometrics bridge. Translates Zigbee commands and fingerprint matches to BLE lock actions via Nuki Smart Lock 3 Pro.

## Build & Flash
```bash
# Source ESP-IDF environment (required)
source /Users/thorstenropertz/.espressif/v5.5.3/esp-idf/export.sh

# From firmware/ directory
idf.py build
idf.py -p <PORT> flash monitor

# Debug profile (verbose logs)
idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.debug.defaults" build

# Release profile (optimized)
idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.release.defaults" build
```

## Testing
Tests live alongside components in `firmware/components/<component>/test/`. The `firmware/test_runner/` project links all components and runs Unity tests on hardware.

```bash
# Source ESP-IDF environment (required)
source /Users/thorstenropertz/.espressif/v5.5.3/esp-idf/export.sh

# Build test runner
cd firmware/test_runner
idf.py build

# Flash and run interactively
idf.py -p <PORT> flash monitor
# Menu shows available tests; run individually or all
```

## Component Structure
Each component exposes public API in `include/` and internals in `src/`:
- `sdf_app` — Application flows (biometric unlock, zigbee bridge, enrollment)
- `sdf_drivers` — Hardware drivers (fingerprint UART, LED, battery, GPIO)
- `sdf_protocol_ble` — BLE/Nuki protocol adaptor
- `sdf_protocol_zigbee` — Zigbee Door Lock cluster adaptor (owns `sdf_zigbee_task`)
- `sdf_services` — Core services (owns `sdf_match_task`, `sdf_enroll_task`, `sdf_admin_task`, `sdf_button_task`)
- `sdf_state_machines` — Enrollment and device state machines
- `sdf_power` — Power manager & sleep (owns `sdf_power_task`)
- `sdf_platform` — ESP32-C6 HAL wrappers
- `sdf_storage` — NVS storage and persistence
- `sdf_config` — Static configuration and defaults
- `sdf_common` — Shared types and utilities
- `sdf_ota` — OTA update mechanism (version, signature, rollback) (owns `sdf_ota_task`, future)

## Critical Setup Steps
1. **Set real lock BLE address** in `firmware/components/sdf_app/src/sdf_app.c` (`SDF_NUKI_TARGET_ADDR_TYPE` and `SDF_NUKI_TARGET_ADDR`). Current values are placeholders.
2. **NimBLE addresses are little-endian**: Lock address `AA:BB:CC:DD:EE:FF` → `{0xFF, 0xEE, 0xDD, 0xCC, 0xBB, 0xAA}`.
3. **NVS encryption** is enabled. Keep `firmware/partition_table.csv` with the `nvs_keys` partition.

## Security Defaults
Configured in `firmware/sdkconfig.defaults`:
- Nonce replay window: 8 entries
- Biometric fail threshold: 5 attempts in 60s → 120s lockout
- Encrypted NVS required at boot
- OTA signature verification: Ed25519 (mandatory)
- OTA downgrade: allowed with warning
- Bootloader rollback: enabled (WDT 90s)

## OTA Commands (CLI)
```bash
ota version          # Print firmware version + build info
ota status           # Show OTA state, partitions, versions
ota trigger zigbee://  # Trigger Zigbee OTA query
ota rollback         # Manual rollback to previous firmware (requires confirmation)
ota verify           # Verify pending OTA partition (version + signature)
```

## Documentation Sync Rule

When making architectural changes, you **must** update the corresponding documentation files. Failure to do so creates drift between code and docs.

**Trigger:** Update docs when you change any of the following:
- Component boundaries (new components, removed components, renamed components)
- Public API of any component (new functions, changed signatures, removed functions)
- Build commands, test commands, or toolchain configuration
- Security defaults or Kconfig options
- Runtime behavior (task structure, power management, enrollment flow)

**Which file to update:**

| Change Type                                           | Update                                 |
| ----------------------------------------------------- | -------------------------------------- |
| Component structure, public API, architecture         | `doc/sdf_sas.md` (sections 5, 6, 8, 9) |
| User-facing behavior, enrollment flow, button mapping | `doc/user_manual.md`                   |
| Build commands, component list, security defaults     | `AGENTS.md`                            |
| Version history                                       | `version.md`                           |

**Verification:** Before completing your task, confirm you checked whether the change affects any of the files above. If it does, update the file. If it does not, you may skip.

## Gotchas
- Fingerprint sensor `Control LED (0x3C)` payload bytes are module-variant specific; defaults in `sdf_services.c` may need tuning on real hardware.
- No CI workflows exist yet. Tests require hardware.
- `scripts/` and `tools/` directories are currently empty.

## Instructions
Make use of codebase-memory-mcp and serena to understand the codebase.