## 0. Prerequisite

- [x] 0.1 Confirm `register-admin-task-watchdog` has landed, including its task 2.3, and
      that `esp_timer_get_time_mock()` is implemented. `sdf_platform_time.c` currently
      carries an undefined reference to it on the Linux target and survives only because
      the linker drops the whole object; the first host-target caller of any
      `sdf_platform_time_*` function turns that into a link error. See design.md Context.

## 1. Finish the platform watchdog surface

- [x] 1.1 Make `sdf_platform_time_wdt_reset()` capture `esp_task_wdt_reset()`'s return
      value and, on `ESP_ERR_NOT_FOUND`, log once per task handle at warning level naming
      the calling task via `pcTaskGetName(NULL)`. Rate-limit per handle, not globally: a
      single offender must not mask a second one.
- [x] 1.2 Keep the signature `void`. Callers must not be asked to check — see design.md
      "The not-found diagnostic is the point of centralising".
- [x] 1.3 Give the Linux implementation the equivalent behaviour so the host suite can
      assert it: a reset from a task that never called `sdf_platform_time_wdt_add()`
      records the same one-shot diagnostic.
- [x] 1.4 Remove `esp_task_wdt_reset_mock` / `esp_task_wdt_reconfigure_mock`, their
      `#define` overrides and the local `esp_task_wdt_config_t` from
      `sdf_mock_linux_time.h`. All are unimplemented and unreachable, and they present a
      second, conflicting portability strategy for the same symbol.
- [x] 1.5 Update `sdf_platform_time_wdt_reset()`'s doc comment (`sdf_platform_time.h:71`):
      "No-op on Linux" stops being true once 1.3 lands.

## 2. Migrate sdf_match_task

- [x] 2.1 Replace the inline `#ifndef` + `esp_task_wdt_add(NULL)` at
      `sdf_services_match.c:274-276` with `sdf_platform_time_wdt_add()`.
- [x] 2.2 Replace the four `esp_task_wdt_reset()` blocks at lines 281-283, 307-309 and
      345-347 with `sdf_platform_time_wdt_reset()`, preserving placement exactly. Do not
      consolidate the two pre-loop resets into the loop — placement is load-bearing and
      documented in the comment at lines 349-356.
- [x] 2.3 Replace `esp_task_wdt_delete(NULL)` at lines 409-411 with
      `sdf_platform_time_wdt_delete()`.
- [x] 2.4 Remove the now-unused `#include "esp_task_wdt.h"` and its `#ifndef` guard at
      lines 12-14.

## 3. Migrate sdf_enroll_task

- [x] 3.1 Replace the inline blocks at `sdf_services_enroll.c:284-286` (add), 291-293 and
      353-355 (reset), and 363-365 (delete) with the corresponding wrapper calls.
- [x] 3.2 Remove the unguarded `#include "esp_task_wdt.h"` at line 12 — the one that
      compiles on the Linux target only because IDF ships the header for all targets while
      excluding the implementation.
- [x] 3.3 Verify no file under `firmware/components/sdf_services/` matches
      `esp_task_wdt` afterwards.

## 4. Tests and verification

- [x] 4.1 Add host coverage in `sdf_platform`'s test for the one-shot not-found
      diagnostic: a reset from an unregistered task records it once, a second reset from
      the same task does not, and a reset from a *different* unregistered task does.
- [x] 4.2 Confirm the existing `test_sdf_services_start_stop_start_tasks_cycle` assertions
      from `register-admin-task-watchdog` still pass for all three tasks, now that match
      and enroll register through the wrapper too.
- [x] 4.3 Build for esp32c6 and diff the disassembly or map of `sdf_match_task` /
      `sdf_enroll_task` against the pre-change build to confirm this is behaviour-neutral.
      A pure substitution should show no change beyond the call indirection.
- [x] 4.4 Do not attempt `esp-emu` verification: the current firmware boot-loops under it
      with a NULL-`ble_hs_mutex` fault before the service tasks reach steady state. See
      `register-admin-task-watchdog`'s design.md for the captured evidence.

## 5. Optional: remaining hand-rolled call sites (separable)

Pull in only if the reviewer wants the class closed rather than the `sdf_services`
instance. Each is independent of groups 1-4 and of the others.

- [ ] 5.1 `fingerprint.c` — `esp_task_wdt_add` at 1098, resets at 404, 1107, 1120, 1142,
      delete at 1126. Highest value of the three: `fp_wait_for_reply()` (1138-1145) is
      called by arbitrary tasks, so it is exactly where the not-found diagnostic earns its
      keep.
- [ ] 5.2 `sdf_power.c` — `esp_task_wdt_add` at 241, reset at 246.
- [ ] 5.3 `sdf_nuki_crypto.c` — resets at 408 and 413, with no `add` in the file; confirm
      which task owns those call sites and whether it is registered before migrating.
      Treat a not-found result here as a finding, not a formality.
- [ ] 5.4 Leave `sdf_app.c:1638` alone — `esp_task_wdt_reconfigure()` is TWDT
      configuration, not per-task participation, and does not belong behind this wrapper.
