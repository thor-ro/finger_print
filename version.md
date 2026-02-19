# SDF Version History

This file tracks firmware-level changes and maps them to project versions.

## 0.1.3 — 2026-02-19

### Added
- SDK configuration options for Phase 5 power management:
  - Zigbee check-in interval
  - idle-before-sleep window
  - post-wake guard window
  - power task loop interval
  - battery reporting interval
  - fingerprint WAKE GPIO
  - light-sleep enable toggle
  - BLE radio gating enable toggle
  - default battery percentage
- New power options menu in `/Users/thorstenropertz/workspace/smart_door/firmware/components/sdf_tasks/Kconfig`.

### Changed
- Replaced hardcoded power values in app/power initialization with `CONFIG_SDF_POWER_*` options.
- Updated default project profile values in `/Users/thorstenropertz/workspace/smart_door/firmware/sdkconfig.defaults`.

## 0.1.2 — 2026-02-19

### Added
- Phase 4 implementation for fingerprint support:
  - UART fingerprint driver with command framing, checksum validation, ACK parsing, timeout handling, and mutex-protected access.
  - Enrollment state machine (`IDLE -> STEP_1 -> STEP_2 -> STEP_3 -> SUCCESS/ERROR`).
  - Fingerprint service task for:
    - periodic `1:N` match polling,
    - enrollment request handling,
    - enrollment progress callbacks,
    - LED feedback hooks.
- App integration for fingerprint flow:
  - fingerprint match triggers local BLE unlock request,
  - Zigbee programming commands (`SET_PIN_CODE` / `SET_RFID_CODE`) trigger enrollment requests.

### Changed
- Component wiring and dependencies for `sdf_drivers`, `sdf_services`, and `sdf_state_machines` to support Phase 4 runtime behavior.
- README important notes updated with fingerprint LED command caveat.

### Notes
- `Control LED (0x3C)` payload bytes are module-variant specific and may require hardware tuning.
- Build validated with ESP-IDF `v5.5.3` and target `esp32c6`.

## 0.1.1 — 2026-02-19

### Added
- Phase 3 Zigbee Door Lock Cluster (ZHA) command routing.
- Lock/unlock/latch command path and state reporting updates.
- Explicit programming-event payload parsing.

## 0.1.0 — 2026-02-19

### Added
- Initial ESP-IDF component structure and build wiring.
- BLE/Nuki client and pairing foundation with NVS-backed credential persistence.
- Baseline project configuration (`CMake`, `sdkconfig.defaults`, partition table, Zigbee/BLE toggles).
