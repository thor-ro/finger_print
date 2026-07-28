## 1. sdf_ota Component Setup

- [x] 1.1 Create component directory structure: `firmware/components/sdf_ota/{include,src,test,CMakeLists.txt,Kconfig}`
- [x] 1.2 Add sdf_ota to `firmware/CMakeLists.txt` component list
- [x] 1.3 Define `sdf_ota.h` public API: `sdf_ota_init()`, `sdf_ota_begin()`, `sdf_ota_write()`, `sdf_ota_verify_and_commit()`, `sdf_ota_verify_integrity()`, `sdf_ota_rollback()`, `sdf_ota_get_version()`, `sdf_ota_get_state()`
- [x] 1.4 Add Kconfig options: `CONFIG_SDF_OTA_ENABLE`, `CONFIG_SDF_OTA_SIGNATURE_VERIFY`, `CONFIG_SDF_OTA_ALLOW_DOWNGRADE`, `CONFIG_SDF_OTA_BOOTLOADER_ROLLBACK`, `CONFIG_SDF_OTA_ZIGBEE_QUERY_INTERVAL_HOURS`

## 2. Version Embedding (Build System)

- [x] 2.1 Create `firmware/cmake/version.cmake` that runs `git describe --tags --always --dirty` and sets `PROJECT_VER`
- [x] 2.2 Include version.cmake in `firmware/CMakeLists.txt` before `project(sdf)`
- [ ] 2.3 Verify `esp_app_get_description()->version` contains correct string at runtime
- [x] 2.4 Add fallback for builds outside git repo (CI): `v0.0.0-<timestamp>`

## 3. Version Comparison Logic

- [x] 3.1 Implement `sdf_ota_version_compare(current, incoming)` in `sdf_ota_version.c`: returns `OLDER`, `EQUAL`, `NEWER`
- [x] 3.2 Handle semantic version parsing: major.minor.patch[-pre][+build]
- [x] 3.3 Pre-release suffixes (commit count, hash) compare lower than release
- [ ] 3.4 Unit tests for version comparison in `firmware/components/sdf_ota/test/test_version.c`

## 4. Ed25519 Signature Verification

- [x] 4.1 Generate Ed25519 keypair for OTA signing (store private key securely, embed public key)
- [x] 4.2 Add public key as compile-time constant in `sdf_ota_signature.c` (or NVS if rotation needed)
- [x] 4.3 Implement `sdf_ota_verify_signature(image_addr, image_size)` using mbedTLS ed25519
- [x] 4.4 Create `tools/sdf_sign_ota.py`: appends 64-byte signature + 4-byte magic (`SDF\x01`) to binary
- [x] 4.5 Add CMake target `sign_ota` that runs sign script post-build
- [ ] 4.6 Unit test: valid signature passes, invalid fails, missing signature fails

## 5. OTA Session State Machine

- [x] 5.1 Define state enum: `IDLE`, `DOWNLOADING`, `VERIFYING`, `COMMITTING`, `COMPLETE`, `FAILED`
- [x] 5.2 Implement state transitions with validation (no IDLE→COMMITTING, etc.)
- [x] 5.3 Track: target partition, bytes written, expected size, version string, signature status
- [x] 5.4 Thread-safe access via mutex (Zigbee task + CLI may call concurrently)

## 6. Core OTA Operations

- [x] 6.1 `sdf_ota_begin(size)`: call `esp_ota_begin()` on next partition, store handle
- [x] 6.2 `sdf_ota_write(data, len)`: call `esp_ota_write()`, update progress, handle errors
- [x] 6.3 `sdf_ota_verify_integrity()`: verify written bytes == expected size
- [x] 6.4 `sdf_ota_verify_and_commit()`: 
  - [x] 6.4.1 Read version from target partition app_desc
  - [x] 6.4.2 Compare with current version (log upgrade/downgrade/reinstall)
  - [x] 6.4.3 If signature verify enabled: verify Ed25519 signature
  - [x] 6.4.4 Call `esp_ota_end()`, `esp_ota_set_boot_partition()`, `esp_restart()`
- [x] 6.5 `sdf_ota_rollback()`: call `esp_ota_mark_app_invalid_rollback_and_reboot()`

## 7. Zigbee OTA Integration

- [x] 7.1 Modify `sdf_protocol_zigbee` to include `sdf_ota.h`
- [x] 7.2 In `sdf_zigbee_ota_upgrade_status_handler`:
  - [x] 7.2.1 `START`: call `sdf_ota_begin(image_size)`
  - [x] 7.2.2 `RECEIVE`: call `sdf_ota_write(payload, payload_size)`
  - [x] 7.2.3 `CHECK`: call `sdf_ota_verify_integrity()`
  - [x] 7.2.4 `APPLY`: call `sdf_ota_verify_and_commit()`
  - [x] 7.2.5 `FINISH`: handled by commit (reboot)
- [x] 7.3 Remove inline `esp_ota_*` calls from zigbee handler
- [x] 7.4 Update query interval via `sdf_ota` config (uses `CONFIG_SDF_OTA_ZIGBEE_QUERY_INTERVAL_HOURS`)
- [x] 7.5 **Progress Reporting**: In `sdf_ota_write()` for ZIGBEE source, call `esp_zb_ota_upgrade_client_progress_report()` with current offset/total size
- [x] 7.6 **Progress Reporting**: In `sdf_ota_verify_and_commit()` for ZIGBEE source, send final 100% progress report before commit

## 8. CLI Commands

- [x] 8.1 Add `ota` command group in `sdf_cli_commands.c`
- [x] 8.2 `ota version`: print `sdf_ota_get_version()` + build timestamp
- [x] 8.3 `ota status`: print current state, current/running partition, version
- [x] 8.4 `ota trigger zigbee://`: initiate Zigbee OTA query (delegate to zigbee stack)
- [x] 8.5 `ota trigger <url>`: placeholder for future HTTP/BLE source (log not implemented)
- [x] 8.6 `ota rollback`: call `sdf_ota_rollback()` (requires admin auth)
- [x] 8.7 `ota verify`: manually trigger verify on pending partition

## 9. Audit Events

- [x] 9.1 Define audit event types: `OTA_TRIGGERED`, `OTA_STARTED`, `OTA_VERIFYING`, `OTA_COMMITTED`, `OTA_ROLLED_BACK`, `OTA_FAILED`
- [x] 9.2 Emit events via `sdf_app` audit callback at each state transition
- [x] 9.3 Include: timestamp, source (zigbee/cli), version, result, error code

## 10. Bootloader Rollback Config

- [x] 10.1 Enable `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y` in `sdkconfig.defaults`
- [x] 10.2 Enable `CONFIG_BOOTLOADER_WDT_ENABLE=y` and `CONFIG_BOOTLOADER_WDT_TIME_MS=90000`
- [ ] 10.3 Test: flash bad firmware, verify automatic rollback on boot

## 11. Tests

- [ ] 11.1 Unit test: version comparison (all cases from spec)
- [ ] 11.2 Unit test: signature verification (valid/invalid/missing)
- [ ] 11.3 Unit test: state machine transitions (valid/invalid)
- [ ] 11.4 Integration test (test_runner): Zigbee OTA flow mock
- [ ] 11.5 Integration test: CLI ota commands
- [ ] 11.6 Manual test: full Zigbee OTA → verify → commit → reboot
- [ ] 11.7 Manual test: CLI rollback → verify previous firmware boots
- [ ] 11.8 Manual test: automatic rollback on bad firmware

## 12. Documentation Updates

- [x] 12.1 Update `doc/sdf_sas.md`: add OTA section in cross-cutting concepts
- [x] 12.2 Update `doc/sdf_sas.md`: update risks table (remove "No OTA update mechanism")
- [x] 12.3 Update `version.md`: add 0.2.0 entry for OTA mechanism
- [x] 12.4 Update `AGENTS.md`: add OTA trigger/rollback to testing section