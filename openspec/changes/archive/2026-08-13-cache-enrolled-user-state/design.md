## Context

See proposal.md - Why for the boot-time race that lets a claimed device be
treated as unclaimed (admin-gate bypass) and the redundant sensor queries this
also removes.

Today, `sdf_services_query_users()` (`sdf_services.c:790-801`) is a thin wrapper
around `fp_query_users()`, a blocking UART round-trip through the fingerprint
owner task (see the `fp-io-owner-task` change, already in progress, which
serializes all `fp_*` calls through a single owner task). Four call sites use it:

- `sdf_match_task`'s boot sequence (`sdf_services_match.c:267-281`), whose result
  seeds `s_state.enrolled_user_count`.
- `sdf_app_update_zigbee_user_list()` (`sdf_app.c:1032-1047`).
- `sdf_services_start_local_enrollment_with_permission()` (`sdf_services.c:222-280`),
  which packs the result into a throwaway static bitmap `s_enrollment_user_bmp` to
  find the lowest free user ID.
- `sdf_services_change_user_permission()` (`sdf_services.c:803-925`), which packs
  the result into a throwaway static bitmap/perm array (`s_perm_user_bmp`,
  `s_perm_perm_packed`) to look up the target user's current permission and count
  admins.

`sdf_services_state_t.enrolled_user_count` (`sdf_services_internal.h:53`) is
separately incremented/decremented/zeroed at each mutation site
(`sdf_services_enroll.c:150`, `sdf_services.c:759-760`, `:779`, `:1010`), except
at boot where it's overwritten wholesale by the query result. It is read by
`sdf_button_dispatch_action()` (`sdf_services_button.c:143`),
`sdf_match_task_run_match_cycle()` (`sdf_services_match.c:104`), and
`sdf_services_get_setup_state()` (`sdf_services.c:1066-1086`) to decide whether
the device has an admin at all.

The bitmap/packed-permission representation (`SDF_SERVICES_BMP_TEST/SET/CLEAR`,
`sdf_services_perm_get/set`, `sdf_services_pack_user_list()` in
`sdf_services_internal.h:97-120`) already exists and is already sized correctly
(10 users, `uint16_t` bitmap, 2-bit-per-user packed permissions) - it's just
rebuilt from a live query every time instead of persisted.

## Goals / Non-Goals

**Goals:**
- Make `sdf_services_query_users()` itself cache-backed, so every existing caller
  benefits without call-site-specific changes.
- Guarantee the cache is correct from the moment any task can read it - no
  post-boot window where it under-reports.
- Keep the sensor and the persisted cache from ever disagreeing, given the
  trust invariant that only this firmware's owner-task API mutates the sensor.

**Non-Goals:**
- No change to the fingerprint owner-task serialization itself (that's
  `fp-io-owner-task`'s scope).
- No periodic reconciliation between sensor and cache after the initial NVS
  load - the trust invariant makes this unnecessary, and if it's ever violated
  (e.g. a debug/CLI path added later that touches the sensor directly), that's a
  new invariant break to handle explicitly, not something this change should
  paper over with polling.
- No change to `sdf_services.h`'s public API surface or wire formats.

## Decisions

### One persisted cache, not a count plus two scratch bitmaps
Replace `enrolled_user_count`, `s_enrollment_user_bmp`, and
`s_perm_user_bmp`/`s_perm_perm_packed` with a single `uint16_t` bitmap +
`uint8_t[3]` packed-permissions pair in `sdf_services_state_t`, protected by the
existing `s_state.lock`. `enrolled_user_count` becomes a `popcount()` over the
bitmap computed on read, not a stored field - it can't drift from the bitmap
it's derived from because it no longer exists independently.

Alternative considered: keep `enrolled_user_count` as a stored field maintained
alongside the bitmap (cheaper reads, no popcount). Rejected - the whole reason
today's field is unreliable is that it's a second piece of state that can fall
out of sync with the real record; computing it on read removes that class of
bug for a negligible cost (population count of a 16-bit value).

### `sdf_services_query_users()` becomes the single redirection point
Change its implementation to serve directly from the cached bitmap/permissions
under `s_state.lock` instead of calling `fp_query_users()`. All four existing
callers keep their current call signature and get the fix (and the latency win)
without their own changes.

Alternative considered: fix each call site individually (skip the boot query,
have enrollment/permission-change read a new cache-access function directly).
Rejected - `sdf_services_query_users()` already the shared choke point; changing
it once is smaller and can't leave a caller out by omission the way four
separate edits could.

### NVS load happens synchronously in `sdf_services_init()`, before task creation
`sdf_services_init()` (`sdf_services.c:635-727`) already zeroes
`enrolled_user_count` at line 680, before calling `sdf_services_start_tasks()`
at line 698. Replace that line with a synchronous NVS read of the persisted
bitmap/permissions into `s_state`. Since `sdf_services_init()`'s caller (app
startup) already blocks until this function returns, and no button/match/admin
task exists yet at this point, there is no code path that can observe the cache
before it's correct.

Alternative considered: keep the sensor query in `sdf_match_task` but move it
earlier / mark tasks not-ready until it completes. Rejected per the explicit
design decision to make NVS authoritative - an NVS read is ~microseconds versus
the sensor query's multi-second UART round trip, so there's no latency reason to
prefer deferring readiness over reading synchronously up front.

### Synchronous, ordered NVS persistence with asymmetric failure handling
On every successful enroll/delete/clear/permission-change:
1. Perform the sensor-side operation (unchanged, via the fingerprint owner task).
2. On sensor success, update the in-RAM bitmap/permissions under `s_state.lock`.
3. Write the updated cache to NVS, retrying with backoff on failure.
4. Only report success to the caller after the NVS write lands.

If NVS retries are exhausted:
- **Enroll path**: roll back by deleting the just-added print from the sensor
  (`fp_delete_user()`), then report enrollment failure. This is the path that
  matters most - an unpersisted enrollment that left the RAM cache advanced (or
  the sensor with an orphan print and the cache not advanced) can reproduce the
  exact boot-race bug this change fixes, this time triggered by a flash fault
  instead of timing. Keeping sensor and cache/NVS strictly consistent avoids
  that.
- **Delete/clear/permission-change path**: retry, then report failure with no
  sensor-side rollback. A cache entry that still shows a deleted user as
  enrolled is inert (the sensor will simply never match that user again) - not
  a security risk, so the extra UART round trip a rollback would cost isn't
  justified here.
- Either way, exhausting retries triggers `led_flash_red()` (the same error
  indication already used elsewhere in `sdf_services.c`, e.g. for insufficient
  heap), so the failure is visible rather than silent.

Alternative considered: fire-and-forget NVS writes (update cache, write NVS,
don't block the caller on the result). Rejected - this reopens exactly the gap
this change exists to close, just moved from "boot" to "any mutation."

## Risks / Trade-offs

- **[Risk]** NVS wear from a write on every enroll/delete/permission-change. →
  **Mitigation**: these are all human-paced operations (a person touching a
  sensor or pressing a button), not a hot path - NVS wear leveling handles this
  write frequency without issue at any realistic usage rate.
- **[Risk]** Enroll-path rollback (`fp_delete_user()` after an NVS failure)
  itself could fail (e.g. sensor UART temporarily unavailable), leaving an
  orphan print on the sensor with the cache correctly *not* advanced. →
  **Mitigation**: this is a fail-safe outcome, not a fail-open one - the cache
  under-reporting a real print means that user simply can't authenticate until
  a future re-enrollment overwrites or reuses that slot; it cannot cause the
  admin-gate-bypass class of bug this change fixes. Surfaced via the same
  `led_flash_red()` path.
- **[Risk]** `popcount()` on every read adds a tiny amount of CPU versus a
  stored counter. → **Mitigation**: negligible (16-bit population count) next
  to the multi-second UART operations this change removes from these same call
  paths.

## Migration Plan

- No stored data to migrate from: today's `enrolled_user_count` is derived
  fresh from the sensor at boot, so a device upgrading to this firmware simply
  has no persisted bitmap in NVS on first boot after the update.
- `sdf_services_init()`'s NVS load SHALL treat "key not found" as "zero users
  enrolled" (not an error), then let the normal enroll path populate and
  persist the cache going forward. This means a device with existing sensor
  enrollments will read as unclaimed once, immediately after this firmware
  update, until its users are re-enrolled or an explicit one-time backfill
  (query the sensor once, write the result to NVS) is run as part of the OTA
  step. See Open Questions.
- Rollback: reverting to a pre-change firmware build resumes the old boot-query
  behavior with no cleanup needed - the new NVS key is simply ignored.

## Open Questions

- Should the OTA path for this specific update include a one-time backfill
  (query the sensor once, seed NVS) so devices already in the field with
  enrolled users don't need re-enrollment after updating? This doesn't change
  the specs, chosen approach, or task breakdown - it only affects whether one
  extra one-shot migration task is needed at rollout time, and can be decided
  during implementation.
- Whether `fp_probe()` alone (without the now-removed `sdf_services_query_users()`
  call) is still worth keeping in `sdf_match_task`'s boot sequence as a pure
  sensor-connectivity/health check for the boot log. Doesn't affect any
  specified behavior either way.
