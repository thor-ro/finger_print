# AGENTS.md — Smart Door Finger (SDF) v2.0

## Project Overview
ESP-IDF v6.0.2 firmware for ESP32-C6 biometrics bridge. Translates Zigbee commands and fingerprint matches to BLE lock actions via Nuki Smart Lock 3 Pro.

## Build & Flash
```bash
# Source ESP-IDF environment (required)
source /Users/thorstenropertz/.espressif/v6.0.2/esp-idf/export.sh

# From firmware/ directory
idf.py build
idf.py -p <PORT> flash monitor

# Debug profile (verbose logs)
idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.debug.defaults" build

# Release profile (optimized)
idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.release.defaults" build
```

## Testing
Tests live alongside components in `firmware/components/<component>/test/`. The `firmware/test_runner/` project links all components and runs Unity tests. It's pinned to `IDF_TARGET=linux` (see `firmware/test_runner/sdkconfig.defaults`), so most suites now run host-side without hardware — no flashing required for day-to-day test runs.

```bash
# Source ESP-IDF environment (required)
source /Users/thorstenropertz/.espressif/v6.0.2/esp-idf/export.sh

# Build and run on the host (linux target — no hardware needed)
cd firmware/test_runner
idf.py build
./build/sdf_test_runner.elf
# Runs all Unity suites sequentially and exits non-zero if any test fails,
# so this doubles as a CI gate.
```

`sdf_app` and the lock-flow suites are still not wired into the `linux` target (they pull in `sdf_ble_companion`/BLE/WiFi/OTA stacks that don't build for `IDF_TARGET=linux`) — see `openspec/changes/fix-test-runner-build/design.md`'s Open Questions for the proposed follow-up (`add-linux-target-sdf-app-support`). They do run on the chip target, which builds and runs under the emulator without hardware:

```bash
# From firmware/test_runner, targeting ESP32-C6. Build out-of-tree so the
# in-tree linux sdkconfig/build stay untouched.
idf.py -B /tmp/tr_hw -D SDKCONFIG_DEFAULTS="sdkconfig.hw.defaults" -D SDKCONFIG=/tmp/sdkconfig.hw set-target esp32c6
idf.py -B /tmp/tr_hw -D SDKCONFIG=/tmp/sdkconfig.hw build
idf.py -B /tmp/tr_hw -D SDKCONFIG=/tmp/sdkconfig.hw merge-bin -o /tmp/tr_merged.bin

# Either run it under the emulator...
esp-emu --chip esp32c6 --firmware /tmp/tr_merged.bin --elf /tmp/tr_hw/sdf_test_runner.elf --timeout 240s --exit-on "Tests "
# ...or flash real hardware
idf.py -B /tmp/tr_hw -D SDKCONFIG=/tmp/sdkconfig.hw -p <PORT> flash monitor
```

The chip-target run is not yet clean: four suites assert host-environment behaviour (the linux sleep-retention and WDT stubs) or need absent peripherals (fingerprint sensor). Everything else passes.

Switching `test_runner` between the two targets rewrites `dependencies.lock` and `managed_components/` in place, since the component manager keys both to the project directory rather than the build directory. Expect churn in `dependencies.lock`, and run `idf.py build` for the `linux` target last so the committed lock stays on the CI target.

## Component Structure
Each component exposes public API in `include/` and internals in `src/`:
- `sdf_app` — Application flows (biometric unlock, zigbee bridge, enrollment). Owns `sdf_app_task`
- `sdf_drivers` — Hardware drivers (fingerprint UART, LED, battery, GPIO). Owns `fp_owner_task` and `led_task`
- `sdf_protocol_ble` — BLE/Nuki protocol adaptor
- `sdf_protocol_zigbee` — Zigbee Door Lock cluster adaptor (owns `sdf_zigbee_task` and `sdf_zb_attr_task`)
- `sdf_event_router` — Typed event bus (owns `sdf_evt_router_task`; subscriber callbacks must not emit)
- `sdf_services` — Core services (owns `sdf_match_task`, `sdf_enroll_task`, `sdf_admin_task`)
- `sdf_state_machines` — Enrollment and device state machines
- `sdf_power` — Power manager & sleep (owns `sdf_power_task`)
- `sdf_platform` — ESP32-C6 HAL wrappers
- `sdf_storage` — NVS storage and persistence
- `sdf_config` — Static configuration and defaults
- `sdf_common` — Shared types and utilities
- `sdf_ota` — OTA update mechanism (version, signature, rollback). Driven by CLI (`ota` commands), Zigbee (automatic query), and BLE companion (`sdf_ble_companion_ota.c`: phone supplies WiFi credentials + HTTPS firmware URL over the OTA characteristic).

## Critical Setup Steps
1. **Set real lock BLE address** in `firmware/components/sdf_app/src/sdf_app.c` (`SDF_NUKI_TARGET_ADDR_TYPE` and `SDF_NUKI_TARGET_ADDR`). Current values are placeholders.
2. **NimBLE addresses are little-endian**: Lock address `AA:BB:CC:DD:EE:FF` → `{0xFF, 0xEE, 0xDD, 0xCC, 0xBB, 0xAA}`.
3. **NVS encryption** is enabled. Keep `firmware/partition_table.csv` with the `nvs_keys` partition.

## Security Defaults
Configured in `firmware/sdkconfig.defaults`:
- Nonce replay window: 8 entries
- Biometric fail threshold: 5 attempts in 60s → 120s lockout
- Encrypted NVS required at boot
- OTA signature verification: ECDSA P-256 over a raw `r‖s` footer, enabled (`CONFIG_SDF_OTA_SIGNATURE_VERIFY=y`)
- OTA downgrade: allowed with warning
- Bootloader rollback: enabled (WDT 90s)

## Power Management Defaults
Configured in `firmware/sdkconfig.defaults`:
- Zigbee check-in interval: 15s
- Idle before sleep: 5s
- Post-wake guard: 1.5s
- Power loop interval: 250ms
- Battery report interval: 60s
- Light sleep: enabled
- BLE radio gating: enabled
- Deep sleep fallback: enabled
- Adaptive check-in: enabled
- Retention memory size: 256 bytes
- Deep sleep min duration: 30s
- Wake sources: Timer, Fingerprint GPIO, USB

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
- Match task uses suspend flag + extended polling (10s) when idle instead of WDT delete/recreate + semaphore-block for deep sleep transitions. This keeps the WDT active and reduces context-switch overhead.
- `.github/workflows/firmware-ci.yml` gates `firmware/**` pushes/PRs with two jobs: `build-firmware` (`idf.py build` for `esp32c6`) and `test-firmware` (host-side Unity via `test_runner`'s `linux` target). The unit test job only covers what `test_runner` links on `linux` — the `sdf_app`/lock-flow suites run only on the chip target (see Testing above) and CI does not run them, so a green `test-firmware` check is not full regression coverage.
- `sdf_ota_version_compare()` lives in `sdf_ota_version.c` only. It used to be duplicated in `sdf_ota.c`, and because `sdf_ota.c` is not compiled for `IDF_TARGET=linux`, the host suite tested one comparator while the device ran the other. Keep it single-definition. It orders `git describe` pre-releases (`<tag>-<commits>-g<hash>`) by commit count, not lexically — that is the form `PROJECT_VER` takes, so changing it changes the OTA upgrade/downgrade gate.
- `scripts/` and `tools/` directories are currently empty.

## Instructions
Make use of codebase-memory-mcp and serena to understand the codebase.