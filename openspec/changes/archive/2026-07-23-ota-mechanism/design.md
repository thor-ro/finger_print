## Context

The Smart Door Finger (SDF) firmware currently supports Zigbee OTA updates via the ESP Zigbee stack's built-in OTA cluster handler in `sdf_protocol_zigbee`. The implementation inline-calls `esp_ota_begin/write/end` directly in the Zigbee callback handlers. There is no version checking, no signature verification, no CLI trigger, and no rollback mechanism beyond what ESP-IDF bootloader provides automatically.

The partition table already defines dual OTA slots (ota_0, ota_1) and an otadata partition for boot state tracking.

### Current State
- **Partition table**: ota_0 (1.9MB), ota_1 (1.9MB), otadata (8KB), nvs_keys (4KB)
- **Zigbee OTA**: Implemented in `sdf_protocol_zigbee.c:616-696` using ESP Zigbee OTA cluster
- **Bootloader**: ESP-IDF default with rollback support via `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`
- **Version tracking**: None (only `esp_app_get_description()` provides build-time version)
- **Signing**: None

### Stakeholders
- Firmware developers (build, sign, deploy)
- Field devices (receive OTA via Zigbee)
- Operators (manual trigger via CLI)

## Goals / Non-Goals

### Goals
1. **New `sdf_ota` component** - Centralized OTA logic reusable by Zigbee, CLI, and future BLE
2. **Three trigger paths** - Zigbee (existing), CLI (new), BLE Peripheral (architecture only)
3. **Semantic version from git** - `v1.2.3[-N-g<hash>]` embedded at build, compared at OTA time
4. **Ed25519 signature verification** - App verifies before commit; private key offline
5. **Rollback** - Automatic (bootloader) + Manual (`ota rollback` CLI)
6. **Audit trail** - All OTA events via existing `sdf_app` audit callback

### Non-Goals
- BLE Peripheral OTA service implementation (architecture placeholder only)
- Encrypted OTA payload (signature provides authenticity; ZigBLE transport already encrypted)
- Delta updates (full image only)
- Multi-image OTA (single app binary)
- Secure boot integration (separate ESP-IDF feature)

## Decisions

### 1. New Component: `sdf_ota`
**Choice**: Create dedicated component `firmware/components/sdf_ota` with public API in `include/sdf_ota.h`

**Rationale**:
- Separation of concerns: Zigbee handler shouldn't contain OTA logic
- Reusability: Same version check, signature verify, partition logic for CLI/BLE
- Testability: Can unit test OTA logic without Zigbee stack

**Alternatives considered**:
- Extend `sdf_storage`: Too broad, OTA is not just storage
- Extend `sdf_services`: Too high-level, OTA is not a "service" in the same sense
- Keep in `sdf_protocol_zigbee`: Blocks CLI/BLE reuse, couples protocol to OTA logic

### 2. Version String Generation: CMake + git describe
**Choice**: Use CMake `configure_file()` to generate `version.c` from `git describe --tags --always --dirty` at configure time. Embed in `.rodata`.

**Rationale**:
- Build-time only dependency (git available on build host)
- No runtime git dependency
- Standard `git describe` format matches semver+commit pattern

**Alternatives considered**:
- Python script at build time: More complex, same result
- Kconfig option: Manual, error-prone
- CI-only injection: Requires CI, not local builds

**Implementation**:
```cmake
# CMakeLists.txt
find_package(Git QUIET)
if(GIT_FOUND)
  execute_process(COMMAND git describe --tags --always --dirty
                  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                  OUTPUT_VARIABLE SDF_GIT_VERSION
                  OUTPUT_STRIP_TRAILING_WHITESPACE)
else()
  set(SDF_GIT_VERSION "v0.0.0-unknown")
endif()
configure_file(version.c.in version.c @ONLY)
```

### 3. Version Comparison: Semantic with Pre-release Ordering (No Anti-Rollback)
**Choice**: Implement custom comparison in `sdf_ota_version.c`:
- Parse major.minor.patch numerically
- Pre-release suffix (`-N-g<hash>`) < release
- Build metadata ignored
- **No minimum version enforcement** - downgrades allowed with warning

**Rationale**: Matches SemVer 2.0 for pre-release handling. `v1.2.3-5-gabc` < `v1.2.3` < `v1.2.4`. No anti-rollback NVS storage; operator intent trusted.

**Alternatives considered**:
- String compare: Wrong (v1.2.10 < v1.2.3)
- ESP-IDF `esp_app_desc_t`: No comparison helper
- NVS anti-rollback: Adds complexity, not needed per requirements

### 4. Signature Verification: Ed25519 in App (Not Bootloader), Mandatory
**Choice**: Verify in `sdf_ota_verify_and_commit()` before `esp_ota_end()`. Embed 32-byte public key in `.rodata`. **Signature mandatory** - abort if missing or invalid.

**Rationale**:
- Bootloader has severe space constraints (~16KB)
- Ed25519 verification ~50KB code + stack
- App has full mbedTLS/libsodium available
- Only need to verify before commit, not on every boot
- Mandatory ensures production images always signed

**Signature format**: Append to binary:
```
[Original binary][64-byte Ed25519 signature][4-byte magic "SDF\x01"]
```
Magic allows detection of signed vs unsigned images.

**Alternatives considered**:
- Bootloader verification: Requires custom bootloader, more complex
- ECDSA P-256: Larger signatures (64 vs 64 bytes), slower
- RSA: Much larger signatures, slower
- Optional signature: Dev builds can use separate unsigned OTA path

### 5. Trigger Architecture: Source-Agnostic Session Handle
**Choice**: `sdf_ota_begin(source, size)` returns opaque `sdf_ota_handle_t`. Subsequent writes use handle. Source enum: `ZIGBEE`, `CLI`, `BLE`.

**Rationale**:
- Single code path for download/verify/commit
- Source-specific logic only in trigger handlers
- Easy to add new sources

**Zigbee integration**: Existing `s_ota_handle`/`s_ota_partition` moved to `sdf_ota`, Zigbee callbacks become thin shims.

### 6. Rollback: Dual Mechanism
**Choice**: 
- **Automatic**: Enable `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y` in sdkconfig.defaults
- **Manual**: `ota rollback` CLI → `esp_ota_mark_app_invalid_rollback_and_reboot()`

**Rationale**: ESP-IDF bootloader handles automatic rollback on boot failure (watchdog, crash loop). Manual rollback gives operator control.

### 7. Audit Events: Extend Existing `sdf_audit_event_type_t`
**Choice**: Add `SDF_AUDIT_OTA_*` types to `sdf_common` audit enum, emit via `sdf_app_set_audit_callback()`.

**Rationale**: Reuses existing observability infrastructure. No new callback system needed.

### 8. Zigbee OTA Query Interval: Configurable via Kconfig
**Choice**: Add `CONFIG_SDF_OTA_ZIGBEE_QUERY_INTERVAL_HOURS` (default 24) in `sdf_ota` Kconfig. Passed to `esp_zb_ota_upgrade_client_query_interval_set()` at init.

**Rationale**: Operator may want faster/slower OTA polling. Kconfig allows build-time configuration without code changes.

### 9. Zigbee OTA Progress Reporting
**Choice**: During `RECEIVE` status, call `esp_zb_ota_upgrade_client_progress_report()` with current offset/total. Implemented in `sdf_ota_write()` when source=ZIGBEE.

**Rationale**: ESP Zigbee OTA cluster expects progress reports. Coordinator uses this to track update status. Required for proper Zigbee OTA compliance.

## Risks / Trade-offs

| Risk | Impact | Mitigation |
|------|--------|------------|
| **Signature verification fails on valid image** | OTA blocked, device stuck on old firmware | Test signing/verification in CI; provide `sdf_sign_ota.py --verify` for pre-deployment check |
| **Version comparison bug** | Downgrade allowed when shouldn't, or upgrade blocked | Comprehensive unit tests for version.c; fuzz test parser |
| **OTA image too large for partition** | Write fails mid-stream, partition corrupted | `sdf_ota_begin()` validates size vs partition size upfront |
| **Power loss during OTA write** | Partition left in invalid state | ESP-IDF OTA APIs atomic per-sector; `esp_ota_end()` validates; bootloader rolls back |
| **Git not available at build** | Version string fallback to unknown | CMake handles missing git gracefully; CI always has git |
| **Private key compromise** | Attacker can sign malicious firmware | Key stored offline; rotate via new firmware with new embedded pubkey |
| **Zigbee OTA race with CLI OTA** | Concurrent sessions corrupt state | `sdf_ota` tracks single active session; reject new trigger if busy |
| **Progress reporting adds overhead** | Minor latency per chunk | Minimal - single function call per chunk; Zigbee stack handles async |

## Migration Plan

1. **Create `sdf_ota` component** with API + version + signature + session logic
2. **Refactor `sdf_protocol_zigbee`** to delegate to `sdf_ota` (remove inline esp_ota_* calls)
3. **Add CLI commands** in `sdf_cli`: `ota trigger`, `ota version`, `ota rollback`, `ota status`
4. **Update build system**: CMake version generation, signing script
5. **Update sdkconfig.defaults**: Enable bootloader rollback
6. **Update partition table**: Verify ota_0/ota_1 sizes sufficient
7. **Integration test**: Zigbee OTA + CLI OTA + rollback + signature verify

### Rollback Strategy
- New firmware includes rollback capability
- If critical bug: `ota rollback` via CLI or wait for watchdog
- Pubkey rotation: New firmware embeds new pubkey; old firmware can't verify new images (safe)

## Open Questions

1. ~~Minimum version enforcement?~~ **Resolved: No - explicitly not enforcing minimum version; downgrades allowed with warning**
2. **Zigbee OTA query interval**: **Resolved: Configurable via `CONFIG_SDF_OTA_ZIGBEE_QUERY_INTERVAL_HOURS` Kconfig (default 24h)**
3. **Signature mandatory or optional?** **Resolved: Mandatory - abort if missing or invalid**
4. **OTA progress reporting to Zigbee coordinator?** **Resolved: Yes - implement in `sdf_ota_write()` for ZIGBEE source**