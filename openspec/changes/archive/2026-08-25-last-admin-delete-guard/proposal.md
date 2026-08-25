## Why

`sdf_services_change_user_permission()` refuses to demote the last remaining admin (`firmware/components/sdf_services/src/sdf_services.c:1078`). `sdf_services_delete_user()` has no equivalent check: its only `ESP_ERR_INVALID_STATE` return is the uninitialised-lock case at `sdf_services.c:894`, and it reaches `fp_delete_user()` without ever consulting the enrolled-user cache. The CLI already prints `Cannot delete user %u: invalid state (may be last admin).` (`firmware/components/sdf_cli/sdf_cli_commands.c:257`) for a guard that does not exist, and `User %u not found.` for an `ESP_ERR_NOT_FOUND` the function never returns.

Deleting the last admin fingerprint makes every admin-fingerprint-gated action permanently unreachable. The BLE Companion pairing window, Enroll-Admin, Nuki re-pair, Zigbee join and Web Registration Authorization all resolve through the single `match->permission == 3` gate at `sdf_services.c:466`. With the serial console build-gated out of production images, factory reset becomes the only way back — destroying templates, Nuki pairing and Zigbee commissioning to recover from one delete.

This lands ahead of `companion-identity`, which binds companion credentials to admin fingerprint users. Under that model deleting the last admin also destroys the last companion account, turning a recoverable-over-UART mistake into a device that cannot be managed at all. The guard has to be real before the join makes it load-bearing.

## What Changes

- `sdf_services_delete_user()` SHALL reject deletion of the last remaining admin, returning `ESP_ERR_INVALID_STATE` before the blocking sensor round-trip rather than after it.
- `sdf_services_delete_user()` SHALL reject deletion of a user that is not enrolled, returning `ESP_ERR_NOT_FOUND`. The admin-count read needs the cache anyway; returning the miss makes the second existing CLI message honest at no extra cost.
- The admin count SHALL be computed from the cached bitmap and packed permissions, with no sensor query — the same source and loop shape `sdf_services_change_user_permission()` already uses.
- `sdf_services_clear_all_users()` is explicitly **not** guarded. It is a deliberate bulk wipe on the factory-reset path, where losing the last admin is the intent.
- The CLI message for the rejection is corrected to state the reason rather than hedge about it.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `sdf-services-tasks`: adds a requirement that the last remaining admin cannot be deleted, and that user deletion validates enrolment against the authoritative cache before touching the sensor.

## Impact

**Firmware**
- `sdf_services`: `sdf_services_delete_user()` gains a pre-flight cache read (enrolment test + admin count) ahead of `fp_delete_user()`. No new locking discipline — the read uses the same snapshot pattern as `sdf_services_change_user_permission()`, and the existing comment at `sdf_services.c:895-900` explaining why `s_state.lock` is not held across the UART round-trip continues to hold.
- `sdf_cli`: `cmd_user_del()` message text only.

**Tests**
- Host unit tests in `firmware/components/sdf_services/test/`, registered in `firmware/test_runner/main/test_runner_main.c`.

**No migration required** — there are no devices in the field.

**Accepted limitation**
`ESP_ERR_INVALID_STATE` remains ambiguous: `sdf_services_change_user_permission()` already returns it for both "last admin" and "service busy", and this change follows that precedent rather than diverging. `companion-user-mgmt` will need distinguishable rejection reasons when these verbs are exposed over BLE, since a companion app cannot render a useful message from an ambiguous code. Introducing that distinction is deferred to the change that needs it.
