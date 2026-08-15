## Why

`sdf_platform_time_wdt_reset()` and `sdf_platform_time_wdt_feed()`
(`sdf_platform_time.h:73`, `:80`) have **zero callers**. They were built as the platform's
watchdog abstraction and nothing ever adopted them. Meanwhile every long-running task in
the firmware hand-rolls the same three-line `#ifndef CONFIG_IDF_TARGET_LINUX` +
`esp_task_wdt_*` block inline.

That duplication is not hypothetical bookkeeping — it has already produced two defects:

1. `sdf_admin_task` was never registered at all, because a missing call in one of three
   near-identical copies is invisible (fixed by `register-admin-task-watchdog`).
2. `fp_wait_for_reply()` (`fingerprint.c:1138-1145`) feeds the watchdog every ~1 s and
   documents that this makes "a caller blocked here exactly as watchdog-safe as one
   blocked directly inside a synchronous `fp_*` UART call used to be". For any caller that
   is not registered, `esp_task_wdt_reset()` returns `ESP_ERR_NOT_FOUND`, the return value
   is discarded, and the guarantee silently does not hold. Nothing in the code says so.

So the abstraction that would have prevented both exists, is dead, and the codebase is
left in the state `register-admin-task-watchdog` deliberately created: admin on the
wrapper, match and enroll on raw calls. This change finishes that migration and gives the
wrapper the one job that justifies centralising it — making a missing registration say so.

## What Changes

- Move `sdf_match_task` and `sdf_enroll_task` onto `sdf_platform_time_wdt_add()` /
  `_wdt_reset()` / `_wdt_delete()`, removing every inline
  `#ifndef CONFIG_IDF_TARGET_LINUX` + `esp_task_wdt_*` block from
  `sdf_services_match.c` and `sdf_services_enroll.c`. After this, no file under
  `sdf_services/` references `esp_task_wdt` directly.
- Remove the unguarded `#include "esp_task_wdt.h"` at `sdf_services_enroll.c:12`. It is
  the include that compiles on the Linux target only because IDF ships the header for all
  targets while excluding the implementation — the same accident that let
  `sdf_services_admin.c` carry a meaningless include for its whole history.
- Make `sdf_platform_time_wdt_reset()` log once per task when the underlying
  `esp_task_wdt_reset()` reports `ESP_ERR_NOT_FOUND`, naming the calling task. This is the
  diagnostic that would have surfaced defect 2 above the moment it was introduced.
- Extend the Linux implementation so the host suite can assert the same, keeping the
  wrapper's two targets behaviourally comparable.

Not in scope, deliberately — see design.md: the three remaining hand-rolled call sites
outside `sdf_services` (`fingerprint.c`, `sdf_power.c`, `sdf_nuki_crypto.c`), and the
`sdf_task_bus_attach()` helper for the shared subscribe + queue-create + attach sequence.
Task group 5 sketches the first of those so it can be pulled in if wanted; it is written
to be separable.

**Depends on `register-admin-task-watchdog`**, which introduces
`sdf_platform_time_wdt_add()` / `_wdt_delete()` and their host-observable Linux
implementation. This change is a no-op without them.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

None. Watchdog participation for the service tasks is already specified by the
"Every Long-Running Service Task Participates In The Task Watchdog" requirement added to
`sdf-services-tasks` by `register-admin-task-watchdog`. This change moves match and enroll
to a different mechanism for satisfying that existing requirement without altering the
behaviour it describes; the added diagnostic is observability, not capability. Accordingly
`skip_specs: true` is set, rather than inventing a requirement to satisfy validation.

## Impact

- `firmware/components/sdf_services/src/sdf_services_match.c` — 5 inline watchdog blocks
  (lines 274-276, 281-283, 307-309, 345-347, 409-411) replaced by wrapper calls.
- `firmware/components/sdf_services/src/sdf_services_enroll.c` — 4 inline blocks (lines
  284-286, 291-293, 353-355, 363-365) plus the stale include at line 12.
- `firmware/components/sdf_platform/src/sdf_platform_time.c`,
  `firmware/components/sdf_platform/include/sdf_platform_time.h` — not-found diagnostic;
  `_wdt_reset()`'s "No-op on Linux" doc comment becomes inaccurate and needs updating.
- `firmware/components/sdf_platform/include/sdf_mock_linux_time.h` — the existing
  `#define esp_task_wdt_reset esp_task_wdt_reset_mock` macro override interacts with the
  wrapper and must be reconciled; see design.md.
- `firmware/components/sdf_services/test/test_sdf_services.c`,
  `firmware/components/sdf_platform/test/` — coverage for the diagnostic.
- No behavioural change on the ESP target beyond one new log line in a case that is
  currently silent and, after `register-admin-task-watchdog`, should never occur.
