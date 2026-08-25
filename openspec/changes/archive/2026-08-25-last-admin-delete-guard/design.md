## Context

See `proposal.md — Why` for motivation. The constraints that shape the approach:

- **The guard already exists once, in the right shape.** `sdf_services_change_user_permission()` (`sdf_services.c:1055-1080`) snapshots the enrolled-user bitmap and packed permissions, counts admins with a `for (id = 1; id <= SDF_SERVICES_MAX_USERS; id++)` loop over `SDF_SERVICES_BMP_TEST` + `sdf_services_perm_get`, and returns `ESP_ERR_INVALID_STATE` when demoting would take `admin_count` below one. Delete needs the same snapshot and the same loop.
- **The sensor call must not move under the lock.** The comment at `sdf_services.c:895-900` records why: `fp_delete_user()` is a blocking UART round-trip of up to ~12 s, already serialized by the fingerprint driver's own mutex, and holding `s_state.lock` across it would stall the match cycle, admin actions and enrollment for that whole window. The guard must therefore be evaluated from a snapshot taken *before* the sensor call, not from state held across it.
- **The cache is authoritative.** `sdf-services-tasks — Enrolled-User Cache Is Authoritative From Boot` establishes the cached bitmap and packed permissions as the source of truth, loaded synchronously during `sdf_services_init()`. Reading it needs no sensor query and no boot-order caveat.
- **No devices in the field.** No compatibility path is required.

## Goals / Non-Goals

**Goals:**
- Make the CLI's existing claim true: a delete that would remove the last admin is refused.
- Refuse before the sensor round-trip, so a rejected delete has no side effects and no 12 s cost.
- Reuse the admin-count derivation `change_user_permission()` already uses, rather than introducing a second way to count admins.

**Non-Goals:**
- Distinguishable rejection reasons. `ESP_ERR_INVALID_STATE` stays overloaded, matching `change_user_permission()`. `companion-user-mgmt` will need to fix this for BLE clients; doing it here would change one call site's contract without changing the other's.
- Guarding `clear_all_users()`. Bulk erase is the factory-reset path and losing the last admin is its point.
- Any change to who may *call* delete. Authorization is unchanged; this is an integrity guard on the operation itself.

## Decisions

### Refuse before the sensor call, not after

The alternative — delete on the sensor, then notice the cache would be left admin-less and re-enrol — is not available: enrolment requires the physical finger. `sdf_services.c:913-916` already documents the one-way nature of this path, choosing "stale-but-safe" (leave the cache bit set) when the NVS persist fails after a successful sensor delete, because there is no sensor-side rollback by design.

So the only correct ordering is: read the cache, decide, and only then touch the sensor. This also means a refused delete is genuinely free — no UART traffic, no wear, no 12 s stall.

### One snapshot serves both checks

The enrolment test and the admin count come from the same bitmap and packed-permission pair, so they are taken together in a single snapshot rather than as two reads that could straddle a mutation. Adding the not-found return costs nothing given the snapshot is being taken anyway, and it retires the second inaccurate CLI message (`sdf_cli_commands.c:254`) alongside the first.

A snapshot taken before the sensor call can in principle go stale — another caller could delete the second admin in the window between the guard and `fp_delete_user()`. That race is pre-existing and unchanged in kind: `change_user_permission()` has the same window, and the mutation paths that could open it (`delete`, `change_permission`, enrolment) are themselves serialized behind the pending-admin-action machinery. Closing it properly would mean holding `s_state.lock` across the UART round-trip, which the component has already decided against for good reason.

### `ESP_ERR_INVALID_STATE` over a new error code

`change_user_permission()` returns `ESP_ERR_INVALID_STATE` for both "would remove the last admin" and "service busy". Introducing a distinct code for delete only would leave the two sibling operations reporting the same condition differently, which is worse than the shared ambiguity. The CLI can state the reason plainly for delete because delete has no busy-state return; the ambiguity is confined to `change_user_permission()`, whose hedged message stays accurate.

## Risks / Trade-offs

- **A single-admin device cannot delete that admin at all.** Recovery from a compromised or unwanted sole-admin fingerprint now requires enrolling a second admin first, or a factory reset. This is the intended trade: the alternative is a device that can be bricked-for-management by one command.
- **The guard depends on cache accuracy.** If the cache ever diverges from the sensor such that it reports more admins than exist, the guard permits a delete it should refuse. `sdf-services-tasks — Synchronous NVS Persistence On Enrollment Mutation` and the rollback rules in `NVS Write Failure Handling` are what keep that from happening; this change adds no new divergence path.

## Open Questions

None.
