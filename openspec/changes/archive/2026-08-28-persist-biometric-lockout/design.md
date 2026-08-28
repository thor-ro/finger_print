# Design

## D1 — Why not persist the deadline

The obvious shape is to store `lockout_until_us` and compare it against `esp_timer_get_time()` at boot. It does not work: `esp_timer_get_time()` counts from esp_timer initialisation, and after a power loss it restarts near 0. A persisted deadline would therefore always compare as already expired, reproducing the current bug through a different route. There is no battery-backed RTC on this board and no trusted network time.

What is persisted instead is a boolean-with-context: *a lockout was armed and had not been served when we last wrote*. Time is measured only within a boot, where `esp_timer_get_time()` is monotonic and trustworthy.

## D2 — Reboot during lockout re-arms the full duration

Given D1's ignorance of elapsed time, the only two options at boot are to serve a full fresh `lockout_duration_ms` or to serve nothing. Serving nothing is today's bug. A full re-arm is the conservative choice and has the right incentive gradient: interrupting power is never cheaper than waiting the lockout out.

The cost is a legitimate user who power-cycles a locked-out device — they wait 120 s from boot instead of the remainder. For a control that is meant to fire rarely, at a default of 120 s, that is acceptable. It is also self-limiting: the lockout clears on the first successful match.

## D3 — Write points and flash wear

Two write points, both already existing decision sites in `sdf_match_task_run_match_cycle()`:

| Event | Existing site | Persisted |
|---|---|---|
| Threshold reached, lockout entered | `emit_lockout = true` (`sdf_services_match.c:219-225`) | armed |
| Lockout expired, or a match succeeded | `lockout_cleared = true` (`:130-135`), success branch (`:228-230`) | cleared |

That is two NVS writes per lockout episode. Persisting the per-attempt counter instead would put one write on every failed scan, which an attacker can pace indefinitely — a flash-wear DoS traded for a marginal gain, since the attacker still has to survive the lockout once the threshold is hit. Rejected; see the accepted limitation in the proposal.

Both writes happen **outside** `s->lock`. The existing code already computes `emit_lockout` / `lockout_cleared` under the lock and acts on them after `xSemaphoreGive()`, so the persistence calls slot in beside the existing `sdf_match_emit_lockout_cleared()` and the emit block with no new locking discipline. This matters: `sdf_services` deliberately does not hold `s->lock` across blocking I/O, and an NVS write is blocking I/O.

## D4 — Restore point

Restore belongs in `sdf_services_init()`, alongside the enrolled-user cache load, so the lockout is in force before the match task is created and cannot lose a race against the first scan. On restore the deadline is computed as `esp_timer_get_time() + lockout_duration_ms` using the config value in force at boot (a config change while locked out therefore applies the new duration, which is the intuitive reading).

A read failure — missing key on a fresh device, or a corrupt record — resolves to "not locked out". A device that has never locked out has no record, and failing open on a corrupt record matches how `sdf_config` treats a failed-validation persisted config: fall back to the safe default and log. The alternative, failing closed, would brick biometric entry on an NVS glitch.

## D5 — Event emission on restore

Restoring a lockout SHALL emit `SDF_EVENT_ROUTER_SECURITY_LOCKOUT` at CRITICAL priority exactly as entering one does, and clearing it at boot-expiry emits the NORMAL clear, so the `security-event-unification` contract holds ("the lockout alarm state and the lockout audit trail both depend on receiving the pair"). Without this the companion Status characteristic would report no alarm while matching is refused, which reads to the user as a broken sensor.

Emission happens after `sdf_services_init()` returns and the event router is running — the restore itself only sets state; the emit rides the first match cycle, reusing the existing `lockout_cleared` path shape rather than emitting from init.

## D6 — Removing the dead retention field

`failed_attempts` is removed from `sdf_power_retention_t` rather than populated. Populating it would create a second mechanism that only covers deep sleep, leaving reboots to NVS — two sources of truth for one control, with the weaker one covering the case an attacker chooses. NVS covers deep sleep too, because from the state's point of view a deep-sleep wake is a boot.

Removal is safe: nothing reads the block. `sdf_power_load_retention()` has no callers, so no reader can be desynchronised by the layout change, and `sdf_power_save_retention()` recomputes the CRC over whatever the struct now is.

## Open questions

- Should a restored lockout also be surfaced in the health report (`sdf_device_state`) distinctly from a live one, so a support reader can tell "locked out since boot" from "locked out just now"? Deferred — the audit event already carries it, and adding a health field is a `companion-device-health` spec change.
