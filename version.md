# SDF Version History

This file tracks firmware-level changes and maps them to project versions.

## Unreleased

### Security
- **Biometric lockout now survives a reset (`persist-biometric-lockout`).** The lockout was held only in RAM, so power-cycling the device cleared it and the rate limit could be bypassed indefinitely by resetting between bursts. The armed state is now latched in NVS and restored in `sdf_services_init()` before the match task exists. A restored lockout re-arms a *full* `lockout_duration_ms` measured from boot rather than a remainder: elapsed wall-clock time is not recoverable across a power loss (no battery-backed RTC, and `esp_timer_get_time()` restarts at zero), so any persisted deadline would always read as already expired. It is written at the two transitions of an episode — entry, and clear by expiry or successful match — never per failed attempt, so an attacker pacing failed scans cannot turn the control into flash wear. A restored lockout announces itself as a CRITICAL `SECURITY_LOCKOUT`, keeping the alarm/clear pairing the companion status and the audit trail both depend on. A missing or unreadable record fails open and is logged; `sdf_storage_erase_all()` clears the latch, so a factory reset still works.
- The dead `failed_attempts` field is removed from `sdf_power_retention_t` rather than populated: a deep-sleep-only mechanism alongside the NVS one would be two sources of truth for one control, with the weaker covering the case an attacker chooses.

### Fixed
- **Chip-target task-watchdog introspection was stubbed out.** `sdf_platform_time_wdt_is_registered()` returned a constant `true` on every non-`linux` target, `sdf_platform_time_wdt_has_warned()` a constant `false` and `sdf_platform_time_wdt_get_warning_count()` a constant `0`, so on real hardware the WDT accessors reported fiction. `is_registered()` now queries `esp_task_wdt_status()`, and the other two read the warned-task table the chip path already maintained. This made three previously-failing on-chip tests pass and is the difference between the WDT accessors being observable and being decorative.
- **The compiled firmware version could be stale.** `firmware/cmake/version.cmake` evaluates `git describe` at CMake *configure* time, so an incremental `idf.py build` into an existing build directory kept stamping the version captured when that directory was first configured — an image built at one commit could report another. The version files under `.git` are now `CMAKE_CONFIGURE_DEPENDS`, forcing a re-configure when HEAD, the index or the reflog moves. This matters because the string reaches `esp_app_desc` and `sdf_ota_version_compare()`'s upgrade/downgrade gate.
- **`sdf_app` did not compile for the chip target.** `sdf_app_test_exports.inc` still called `sdf_app_map_lock_state_to_zigbee()`, which had been split into `sdf_app_map_nuki_state_to_device()` + `sdf_app_map_device_lock_state_to_zigbee()`; the shim now composes the two, which reproduces the original mapping. The host target never caught this because it does not compile `sdf_app`.

### Added
- `test_sdf_platform_sleep_retention_chip_roundtrip` — asserts a real RTC-retention write/read round-trip on chip, covering what the linux `..._sleep_retention_linux_noops` test can only stub.
- 15 tests for the persisted lockout: the storage latch's round-trip, absent/cleared/erase-all states and null guard; and, driven through the real match-cycle body against the host mock sensor, lockout entry persisting, sub-threshold attempts writing nothing, expiry and successful-match clearing, restore arming a full duration from boot, restore refusing to scan, absent/wrong-typed/unreadable records permitting it, and the restored lockout's CRITICAL announcement being emitted exactly once.
- `firmware/lockout_reset_gate` + `scripts/run_lockout_reset_gate.sh` — a hardware-free esp-emu gate for the one claim no unit test on either target can make: that a lockout armed before a device reset is still in force after it. One image, two boots separated by a real `esp_restart()`; boot 2 asserts the restore re-armed a full duration from that boot, that a match cycle refuses to reach the sensor, and that the restored lockout announces itself once as a CRITICAL `SECURITY_LOCKOUT`.

## 0.2.2 — 2026-08-20

### Changed
- **OTA signature verification**: Replaced the project's hand-rolled ECDSA P-256 footer scheme (streaming SHA-256 digest, mbedTLS verification core, 68-byte `SDF\x01` footer) with ESP-IDF's own signed-app verification (`CONFIG_SECURE_SIGNED_APPS_NO_SECURE_BOOT`, checked inside `esp_ota_end()`). No eFuse is burned and hardware Secure Boot V2 stays disabled; the trust anchor is the running app's own signature block.
- Signing is now part of `idf.py build` (`CONFIG_SECURE_BOOT_BUILD_SIGNED_BINARIES`) rather than a separate `idf.py sign_ota` step. `tools/sdf_sign_ota.py` and the `sign_ota`/`ota_extract_pubkey` CMake targets are removed.
- The signing key moved from `ota_private.key`/`ota_public.key` to a single project-root PEM, `ota_signing_key.pem` (P-256), auto-generated by `firmware/components/sdf_ota/CMakeLists.txt` on first build or injected in CI from the `OTA_SIGNING_KEY` repository secret.
- Removed `CONFIG_SDF_OTA_SIGNATURE_VERIFY` and the CLI `ota verify` subcommand — verification is unconditional and reported by whether the OTA commit succeeds.
- `SDF_OTA_MIN_IMAGE_SIZE` no longer includes a signature-footer term (drops from 356 to 288 bytes: `SDF_OTA_APP_DESC_OFFSET + SDF_OTA_APP_DESC_SIZE`), since ESP-IDF validates the signature sector's presence and position itself.

### Notes
- **Provisioning requirement**: the first image on any device must be flashed signed over serial. Signature verification trusts the *running* app's own signature block (there is no eFuse-backed key), so a device booted from an unsigned or wrongly-signed image cannot accept any subsequent OTA update.
- **No over-the-air rollback across this boundary**: this format and the previous footer-based one are mutually unreadable. Crossing between them in either direction requires a serial reflash.
- **Key rotation requires a serial reflash** and cannot be staged over the air, for the same running-app-trust-anchor reason.

## 0.2.1 — 2026-07-29

### Changed
- Updated GPIO pin assignments to match revised hardware wiring (doc/wiring.md):
  - FP TX (UART TX to sensor): GPIO 0 (was GPIO 4)
  - FP RX (UART RX from sensor): GPIO 1 (was GPIO 5)
  - Battery ADC: GPIO 5 (was GPIO 0)
  - Enrollment/Local Button: GPIO 4 (was GPIO 14)
- `sdkconfig.defaults`: `CONFIG_SDF_ENROLLMENT_BTN_GPIO` updated from 14 to 4.
- `sdf_power/Kconfig`: `SDF_ENROLLMENT_BTN_GPIO` default updated from 14 to 4.
- `sdf_services.c`: UART default constants updated to match new wiring.

## 0.2.0 — 2026-07-23

### Added
- **sdf_ota component**: Complete OTA update mechanism with version management, signature verification, and rollback support.
  - Semantic version embedding from git tags (`v1.2.3[-N-g<hash>]` format) at build time via CMake.
  - Ed25519 signature verification on OTA images (mandatory, aborts if missing/invalid). *(Superseded 2026-08-10: replaced by ECDSA P-256 over a streaming SHA-256 digest — the Ed25519 path called an mbedTLS API that does not exist in ESP-IDF and was only ever compiled with verification disabled.)*
  - Three trigger paths: Zigbee OTA (existing, enhanced), CLI (`ota trigger`), BLE Peripheral (architecture placeholder).
  - Version comparison with semantic ordering (pre-release < release).
  - Automatic rollback on boot failure (bootloader) + manual rollback via CLI (`ota rollback`).
  - Progress reporting to Zigbee coordinator during download.
- **CLI OTA commands**: `ota version`, `ota status`, `ota trigger`, `ota rollback`, `ota verify`.
- **Audit events**: Full OTA lifecycle tracking (triggered, started, verifying, committed, rolled back, failed, signature invalid/missing, version upgrade/downgrade).
- **Build system**: `tools/sdf_sign_ota.py` for signing/verifying images, CMake targets `sign_ota` and `ota_extract_pubkey`. *(Superseded 2026-08-20: replaced by ESP-IDF's own signed-app verification — signing is now part of `idf.py build`, and `tools/sdf_sign_ota.py`/`sign_ota`/`ota_extract_pubkey` are removed. See the 0.2.2 entry.)*
- **Bootloader rollback**: Enabled `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` with WDT.

### Changed
- **sdf_protocol_zigbee**: Refactored OTA handler to delegate to sdf_ota component; removed inline esp_ota_* calls.
- **sdf_common**: Added OTA audit event types (SDF_AUDIT_OTA_*).
- **sdf_app**: Exported `sdf_app_emit_audit()` for OTA component audit emission.

### Notes
- OTA images must be signed with `sdf_sign_ota.py` before distribution. *(Superseded 2026-08-20: signing is now performed by `idf.py build` itself; see the 0.2.2 entry.)*
- Public key embedded in firmware; rotation requires rebuild. *(Superseded 2026-08-20: there is no embedded public key any more — verification trusts the running app's own signature block; rotation requires a serial reflash, not just a rebuild.)*
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
