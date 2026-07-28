## Why

The SDF CLI component (`sdf_cli`) exists and provides authentication infrastructure, but 8 out of 11 command subcommands are stubs returning "not fully implemented yet". Users who connect via USB-C for debugging, provisioning, or recovery cannot manage users, Nuki pairing, or Zigbee network state through the CLI — they must use Zigbee commands from a smart home coordinator instead. This limits standalone device management and field diagnostics.

## What Changes

**New CLI command implementations** (replacing stubs in `sdf_cli_commands.c`):

| Command | Subcommands | Backend API |
|---------|-------------|-------------|
| `user` | `add <id> <perm>`, `get <id>`, `del <id>`, `list` | `sdf_services` + `fingerprint.h` |
| `nuki` | `status`, `connect`, `pair`, `unpair` | `sdf_protocol_ble` + `sdf_nuki_pairing` |
| `zigbee` | `status`, `connect` (permit join), `unpair` (factory reset) | `sdf_protocol_zigbee` |

**No breaking changes** — only adds functionality to existing command structure.

## Capabilities

### New Capabilities
- `cli-user-management`: Full CRUD for fingerprint users via CLI (add, get, delete, list, permission)
- `cli-nuki-management`: Nuki lock status, connection control, pairing, unpairing via CLI
- `cli-zigbee-management`: Zigbee network status, permit join, network leave via CLI

### Modified Capabilities
- None — existing capabilities (`cli-auth`, `cli-factory-reset`) unchanged

## Impact

**Files to modify:**
- `firmware/components/sdf_cli/sdf_cli_commands.c` — implement all stub commands

**Backend APIs used (already implemented):**
- `fingerprint.h`: `fp_query_users()`, `fp_delete_user()`, `fp_query_user_permission()`, `fp_change_user_permission()`, `fp_enroll_step()`
- `sdf_services.h`: `sdf_services_query_users()`, `sdf_services_delete_user()`, `sdf_services_clear_all_users()`, `sdf_services_change_user_permission()`
- `sdf_protocol_ble.h`: `sdf_nuki_ble_is_ready()`, `sdf_nuki_ble_start()`, `sdf_nuki_ble_stop()`
- `sdf_nuki_pairing.h`: `sdf_nuki_pairing_init()`, `sdf_nuki_pairing_start()`, `sdf_nuki_pairing_get_credentials()`
- `sdf_protocol_zigbee.h`: `sdf_protocol_zigbee_is_ready()`, `sdf_protocol_zigbee_permit_join()`, `sdf_protocol_zigbee_factory_reset()`

**Tests to add:**
- `firmware/components/sdf_cli/test/test_sdf_cli.c` — Unity tests for new command handlers

**Documentation to update:**
- `doc/user_manual.md` — add CLI command reference section