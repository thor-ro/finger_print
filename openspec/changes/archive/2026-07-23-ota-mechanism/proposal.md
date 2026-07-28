## Why

The Smart Door Finger (SDF) firmware currently lacks an OTA (Over-The-Air) update mechanism despite having dual OTA partitions (ota_0, ota_1) configured in the partition table. The existing Zigbee OTA implementation is passive (coordinator-driven) and incomplete. Adding a comprehensive OTA mechanism with version management, multiple trigger paths, signature verification, and rollback capability is essential for production deployment and field updates.

## What Changes

- **New sdf_ota component**: Standalone component handling OTA logic, version management, and rollback
- **Version management**: Semantic version from git tag (v1.0.3-commitnumber format) embedded at build time
- **Three OTA trigger mechanisms**:
  - Zigbee OTA (existing path, enhanced with version check)
  - CLI command: `ota trigger <source>` for manual/local updates
  - Future BLE Peripheral OTA service (architecture prepared)
- **Version validation**: Check incoming firmware version before commit; downgrades allowed
- **Signature verification**: Verify OTA image signature (ed25519/ECDSA) before applying
- **Rollback mechanisms**:
  - Automatic rollback on boot failure (ESP-IDF bootloader support)
  - Manual rollback via CLI: `ota rollback`
- **No encryption**: OTA payload not encrypted; integrity via signature only

## Capabilities

### New Capabilities
- `ota-mechanism`: Core OTA update orchestration, version management, trigger handling, and rollback
- `ota-version`: Semantic version embedding and runtime version reporting
- `ota-signature`: Image signature verification before commit

### Modified Capabilities
- `zigbee-ota`: Enhanced to integrate with new sdf_ota component for version check and commit/rollback coordination

## Impact

- **New component**: `firmware/components/sdf_ota/` (CMake, Kconfig, include, src, test)
- **Modified**: `firmware/components/sdf_protocol_zigbee/` - integrate with sdf_ota for version check
- **Modified**: `firmware/components/sdf_cli/` - add `ota` command group
- **Modified**: `firmware/CMakeLists.txt` - include sdf_ota component
- **Build system**: Git version embedding via CMake/ESP-IDF project configuration
- **Partition table**: Already supports dual OTA slots (no change needed)
- **Bootloader**: May need `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` for automatic rollback