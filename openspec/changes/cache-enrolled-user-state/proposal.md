## Why

`sdf_services_state_t.enrolled_user_count` is only made truthful by a live sensor
query that runs inside `sdf_match_task` at boot — a multi-second UART round trip.
Until that query returns, the field sits at its struct-init default of 0, and two
call sites treat `enrolled_user_count == 0` as "device is unclaimed, no admin to
authorize." `sdf_button_dispatch_action()` executes admin actions (including
enrollment) immediately, with no fingerprint gate, whenever the count reads 0. So
for several seconds after every power-cycle, a **claimed** device with a real
enrolled admin behaves like an unclaimed one: a physical attacker who power-cycles
the door and presses the button fast enough can enroll their own fingerprint with
zero authorization.

Every caller of `sdf_services_query_users()` — the boot-time check, Zigbee user
list sync, local-enrollment free-ID assignment, and permission-change lookup —
independently pays that same multi-second UART cost on every call, and the two
non-boot callers each rebuild a throwaway scratch bitmap that's discarded
immediately after use.

## What Changes

- Add a single cached user bitmap + packed permissions to `sdf_services_state_t`,
  persisted to NVS, replacing the three separate representations in use today
  (`enrolled_user_count`, `s_enrollment_user_bmp`, `s_perm_user_bmp` +
  `s_perm_perm_packed`).
- Load the persisted bitmap + permissions from NVS synchronously in
  `sdf_services_init()`, before `sdf_services_start_tasks()` creates the
  button/match/admin/enroll tasks, so there is no window after boot where the
  cache can read as "no users enrolled" on a claimed device.
- **BREAKING (internal only)**: redirect `sdf_services_query_users()` to read the
  cached bitmap/permissions instead of calling `fp_query_users()` on the sensor.
  Every existing caller (boot check, Zigbee user list sync, local-enrollment
  free-ID assignment, permission-change lookup) gets this for free and stops
  touching the sensor, on the invariant that the sensor is only ever mutated
  through this firmware's fingerprint owner-task API.
- `enrolled_user_count` becomes a computed value (population count of the cached
  bitmap) rather than a separately maintained/stored field.
- Enroll/delete/clear/permission-change update the cache and persist to NVS
  synchronously (before returning success to the caller):
  - Enroll-path NVS write failure: retry with backoff; if retries are exhausted,
    roll back the sensor-side enrollment (`fp_delete_user()`) and return failure,
    so sensor and cache/NVS never disagree. Trigger `led_flash_red()`.
  - Delete-path NVS write failure: retry with backoff, then `led_flash_red()` on
    persistent failure. No sensor-side rollback needed (a stale "still enrolled"
    cache entry for an already-deleted print is inert, not a security risk).

## Capabilities

### New Capabilities
(none)

### Modified Capabilities
- `sdf-services-tasks`: the "User Query Buffer Sizing" requirement's local-
  enrollment and permission-change scenarios (openspec/specs/sdf-services-tasks/spec.md:411-429)
  no longer describe a live sensor query; new requirements cover the NVS-backed
  cache as the sole source of truth for enrolled-user state, its synchronous
  persistence on mutation, and its failure handling.

## Impact

- `firmware/components/sdf_services/src/sdf_services.c`: `sdf_services_init()`,
  `sdf_services_delete_user()`, `sdf_services_clear_all_users()`,
  `sdf_services_query_users()` callers, `sdf_services_change_user_permission()`,
  `sdf_services_start_local_enrollment_with_permission()`,
  `sdf_services_get_setup_state()`, `sdf_services_reset_state()`.
- `firmware/components/sdf_services/src/sdf_services_match.c`: `sdf_match_task()`
  boot sequence (removes the boot-time sensor query), match-cycle "no users"
  gate.
- `firmware/components/sdf_services/src/sdf_services_button.c`:
  `sdf_button_dispatch_action()`, `sdf_button_resolve_single_click_action()` (read
  path only, both already go through the shared count/setup-state accessors).
- `firmware/components/sdf_services/src/sdf_services_enroll.c`: enrollment
  completion path (cache update + synchronous NVS persistence instead of a bare
  `enrolled_user_count++`).
- `firmware/components/sdf_services/include/sdf_services_internal.h`: state
  struct gains the persisted bitmap/permissions fields; existing
  `SDF_SERVICES_BMP_*` macros and `sdf_services_perm_get/set` helpers are reused
  against the new persisted copy instead of throwaway scratch copies.
- `firmware/components/sdf_storage`: new NVS blob (namespace/key) for the
  persisted bitmap + packed permissions, following the existing
  `sdf_storage_nuki_save/load` pattern.
- No public API surface change to `sdf_services.h`.
