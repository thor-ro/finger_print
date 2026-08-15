## Why

`sdf_admin_task` has never been registered with the ESP task watchdog. It includes
`esp_task_wdt.h` (`sdf_services_admin.c:13`) but calls nothing from it — no
`esp_task_wdt_add()`, no `esp_task_wdt_reset()`, no `esp_task_wdt_delete()`.
`git log -S "esp_task_wdt_add"` on that file returns nothing: the include is a
copy-paste fossil from `sdf_services_match.c`, stripped of the calls it existed for.

Its two sibling tasks are both registered (`sdf_services_match.c:275`,
`sdf_services_enroll.c:285`). A 15s TWDT with `trigger_panic = true` is configured in
`sdf_app_init` (`sdf_app.c:1638`), so a wedged match or enroll task reboots the device.
A wedged admin task does not — it silently stops servicing button presses,
`ADMIN_ACTION_REQUEST` events and admin-action timeouts, with no reboot, no log and no
external symptom beyond "the button stopped working". `sdf_admin_task` calls into
`sdf_services_execute_admin_action()` (factory reset, Nuki pairing, enrollment
dispatch), which is exactly the kind of long-running work the watchdog exists to catch.

## What Changes

- Register `sdf_admin_task` with the task watchdog on entry, reset it once per
  iteration of its main loop, and deregister on cooperative shutdown — matching the
  lifecycle already used by `sdf_match_task` and `sdf_enroll_task`.
- Add `sdf_platform_time_wdt_add()` / `sdf_platform_time_wdt_delete()` alongside the
  existing `sdf_platform_time_wdt_reset()` (`sdf_platform_time.h:73`), which today has
  no registration siblings. This is the only route to host-side regression coverage:
  IDF's `esp_system` excludes `task_wdt/task_wdt.c` from the `linux` target build, so
  direct `esp_task_wdt_*` calls cannot be linked or observed by `test_runner`, while
  the header still resolves (which is why the unguarded include at
  `sdf_services_enroll.c:12` compiles).
- Give the Linux implementation an observable registration count so
  `test_sdf_services.c` can assert that a started admin task is watchdog-registered and
  a stopped one is not.
- Remove the now-meaningless `esp_task_wdt.h` include from `sdf_services_admin.c`.

Explicitly **not** in scope: migrating `sdf_match_task` / `sdf_enroll_task` off their
inline `#ifndef CONFIG_IDF_TARGET_LINUX` + `esp_task_wdt_*` blocks, and the
`sdf_task_bus_attach()` helper that would collapse the per-task attach boilerplate
across all three. Both are follow-on refactors; this change fixes the defect and
builds only the abstraction that fixing it testably requires.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `sdf-services-tasks`: adds a requirement that every long-running `sdf_services` task
  is watchdog-registered for its whole lifetime, so a wedged task trips the TWDT rather
  than failing silently. The existing `sdf_admin_task` section documents its core loop
  but is silent on watchdog participation; that gap is what let the omission survive.

## Impact

- `firmware/components/sdf_services/src/sdf_services_admin.c` — watchdog add/reset/delete
  in `sdf_admin_task`; drop the stale include.
- `firmware/components/sdf_platform/include/sdf_platform_time.h`,
  `firmware/components/sdf_platform/src/sdf_platform_time.c` — new `wdt_add` /
  `wdt_delete` wrappers.
- `firmware/components/sdf_platform/include/sdf_mock_linux_time.h` (and its
  implementation) — observable registration state for the host target.
- `firmware/components/sdf_services/test/test_sdf_services.c` — assertions folded into
  the existing `test_sdf_services_start_stop_start_tasks_cycle` coverage.
- Behavioural risk is narrow. `sdf_services_execute_admin_action()` — the heavyweight
  path, including the blocking `fp_change_user_permission()` UART round-trip — does not
  run on the admin task at all; it runs on the match task via
  `sdf_services_try_claim_admin_action()` (`sdf_services_match.c:247`), which is already
  watchdog-registered. The admin task's own loop only does bookkeeping: set
  `pending_admin_action`, pulse an LED, check the action timeout. Its idle wait is capped
  at `SDF_ADMIN_IDLE_WAIT_CAP_MS` (1000ms), comfortably inside a 15s window.
- The one admin-task path that can block for an unbounded time is
  `sdf_services_try_bootstrap_admin_action()` (`sdf_services.c:330`), which on an
  unclaimed device invokes the host `admin_action_cb` synchronously for non-ENROLL
  actions (Nuki pairing, factory reset). That callback's duration is owned by `sdf_app`,
  not `sdf_services` — see design.md.
