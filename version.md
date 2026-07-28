# SDF Version History

This file tracks firmware-level changes and maps them to project versions.

## 0.2.0 — 2026-07-23

### Added
- **sdf_ota component**: Complete OTA update mechanism with version management, signature verification, and rollback support.
  - Semantic version embedding from git tags (`v1.2.3[-N-g<hash>]` format) at build time via CMake.
  - Ed25519 signature verification on OTA images (mandatory, aborts if missing/invalid).
  - Three trigger paths: Zigbee OTA (existing, enhanced), CLI (`ota trigger`), BLE Peripheral (architecture placeholder).
  - Version comparison with semantic ordering (pre-release < release).
  - Automatic rollback on boot failure (bootloader) + manual rollback via CLI (`ota rollback`).
  - Progress reporting to Zigbee coordinator during download.
- **CLI OTA commands**: `ota version`, `ota status`, `ota trigger`, `ota rollback`, `ota verify`.
- **Audit events**: Full OTA lifecycle tracking (triggered, started, verifying, committed, rolled back, failed, signature invalid/missing, version upgrade/downgrade).
- **Build system**: `tools/sdf_sign_ota.py` for signing/verifying images, CMake targets `sign_ota` and `ota_extract_pubkey`.
- **Bootloader rollback**: Enabled `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` with WDT.

### Changed
- **sdf_protocol_zigbee**: Refactored OTA handler to delegate to sdf_ota component; removed inline esp_ota_* calls.
- **sdf_common**: Added OTA audit event types (SDF_AUDIT_OTA_*).
- **sdf_app**: Exported `sdf_app_emit_audit()` for OTA component audit emission.

### Notes
- OTA images must be signed with `sdf_sign_ota.py` before distribution.
- Public key embedded in firmware; rotation requires rebuild.
- Downgrades allowed by default (warning logged); disable via `CONFIG_SDF_OTA_ALLOW_DOWNGRADE=n`.

## 0.1.6 — 2026-07-22

### Added
- **sdf_platform component**: ESP32-C6 HAL wrappers for GPIO/ISR, sleep/wake, time/timer, and NVS security initialization. Includes Linux mock implementations for test_runner.
- **sdf_config component**: Centralized runtime configuration management with `sdf_config_t` struct, defaults from Kconfig, validation, and dump API.
- Migrated `sdf_services`, `sdf_power`, `sdf_storage`, `sdf_app`, `sdf_drivers` to use `sdf_platform` APIs instead of inline ESP-IDF HAL calls.
- Updated `sdf_app` to use `sdf_config` for configuration values.
- Updated test_runner CMakeLists to include new components.

### Changed
- Architecture now matches documentation (sdf_platform and sdf_config no longer missing).
- Removed risk entry for missing components from sdf_sas.md.

## 0.1.5 — 2026-07-22

### Added
- Complete factory reset capability (`complete-factory-reset` OpenSpec change):
  - `sdf_storage_erase_all()` and `sdf_storage_ble_target_clear()` to wipe NVS persistent storage.
  - `sdf_protocol_zigbee_factory_reset()` to leave network and clear Zigbee NVRAM.
  - `sdf_services_reset_state()` for in-memory service state clearing.
  - Full execution pipeline in `sdf_app_on_admin_action()` for `FACTORY_RESET` admin action.
  - `factory_reset YES` CLI command in `sdf_cli` with confirmation check and subsystem reset sequence.

## 0.1.4 — 2026-02-19

### Added
- Phase 6 security and hardening baseline:
  - BLE encrypted-message nonce replay detection window with per-authorization tracking.
  - Biometric failed-attempt rate limiting with configurable threshold/window/lockout.
  - App-level audit hook API (`sdf_app_set_audit_callback`) and structured audit events.
  - Boot-time secure-storage policy verification for encrypted NVS and `nvs_keys` partition.
- New Kconfig security menu in `/Users/thorstenropertz/workspace/smart_door/firmware/components/sdf_config/Kconfig`.

### Changed
- `sdkconfig.defaults` now includes security knobs for nonce replay window, biometric lockout policy, and required encrypted NVS.
- `sdf_services` now emits security events for failed matches, lockout enter/clear, and successful match.
- `sdf_app` now maps security events to Zigbee alarm bits and audit events.

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
