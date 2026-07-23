## Why

The Smart Door Finger (SDF) v2.0 firmware has a factory reset admin action that is currently a stub — it only logs a TODO message and does nothing. Users have no way to fully reset the device to its unclaimed state (0 enrolled users, no Nuki pairing, no Zigbee network). This is a critical gap for device resale, troubleshooting, and development workflows. The implementation is straightforward: erase all persistent data and reboot.

## What Changes

- Implement the factory reset handler in `sdf_app_on_admin_action()` (currently a TODO at `sdf_app.c:486`)
- Add `sdf_storage_erase_all()` to wipe the entire NVS namespace (Option B: `nvs_flash_erase()` + re-init)
- Add `sdf_storage_security_clear()` to clear security state (failed attempts, lockout, nonce cache)
- Add `sdf_storage_ble_target_clear()` to clear BLE target address
- Add `sdf_protocol_zigbee_factory_reset()` to leave Zigbee network and erase NVRAM
- Add `sdf_services_reset_state()` to reset in-memory state to defaults
- Add CLI command `factory_reset` for testing
- Add LED feedback (red flash pattern) during reset sequence

## Capabilities

### New Capabilities

- `factory-reset`: Complete factory reset implementation that erases all persistent data (NVS, fingerprint templates, Zigbee NVRAM) and reboots to unclaimed state

### Modified Capabilities

- `sdf-app`: Modified to implement the FACTORY_RESET admin action case (currently a TODO)
- `sdf-storage`: Modified to add `sdf_storage_erase_all()` and `sdf_storage_security_clear()` functions
- `sdf-protocol-zigbee`: Modified to add `sdf_protocol_zigbee_factory_reset()` function
- `sdf-services`: Modified to add `sdf_services_reset_state()` for in-memory state reset
- `sdf-cli`: Modified to add `factory_reset` CLI command

## Impact

- **Code**: `firmware/components/sdf_app/src/sdf_app.c` (TODO at line 486), `firmware/components/sdf_storage/` (new functions), `firmware/components/sdf_protocol_zigbee/` (new function), `firmware/components/sdf_services/` (reset function), `firmware/components/sdf_cli/` (new command)
- **Headers**: `sdf_storage.h`, `sdf_protocol_zigbee.h`, `sdf_services.h`, `sdf_cli.h`
- **Tests**: New unit tests for `sdf_storage_erase_all()`, `sdf_storage_security_clear()`, `sdf_protocol_zigbee_factory_reset()`
- **Documentation**: Update `doc/user_manual.md` (factory reset section), `doc/sdf_sas.md` (security section), `AGENTS.md` (if new Kconfig options added)
- **Breaking**: No breaking changes — factory reset is a new destructive action that was previously a no-op