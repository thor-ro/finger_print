## Context

Three constraints shape this, all discovered by reading the current tree rather than
assumed:

**1. The watchdog implementation does not exist on the host target.** IDF v6.0.2's
`components/esp_system/CMakeLists.txt` registers a short explicit source list when
`target STREQUAL "linux"`; `task_wdt/task_wdt.c` appears only in the non-linux branch,
gated on `CONFIG_ESP_TASK_WDT_EN`. The *header* is on the include path for all targets,
which is why `sdf_services_enroll.c:12` gets away with an unguarded `#include
"esp_task_wdt.h"` while carefully guarding every call. So the existing `#ifndef
CONFIG_IDF_TARGET_LINUX` guards around calls are load-bearing — removing them breaks the
host link — and no host test can observe a direct `esp_task_wdt_*` call.

**2. A half-built abstraction already exists.** `sdf_platform_time.h:73` declares
`sdf_platform_time_wdt_reset()`, implemented at `sdf_platform_time.c:47` with the
`#ifndef` inside it, plus `sdf_platform_time_wdt_feed()`. `sdf_mock_linux_time.h` already
carries `esp_task_wdt_reset_mock` / `esp_task_wdt_reconfigure_mock`. There is no
`wdt_add` / `wdt_delete` sibling, and the three service tasks bypass the wrapper
entirely, hand-rolling the `#ifndef` + raw call inline.

**3. The host target really does run these tasks.**
`test_sdf_services_start_stop_start_tasks_cycle` (`test_sdf_services.c:897`) calls
`sdf_services_start_tasks()` for real and asserts on `match_task` / `enroll_task` /
`admin_task` handles across a full start → stop → start cycle. That is the natural place
to hang a registration assertion, and it means the assertion costs no new fixture.

## Goals / Non-Goals

**Goals:**

- `sdf_admin_task` participates in the task watchdog for its whole lifetime.
- The omission becomes detectable by the existing host suite, so it cannot silently
  recur for a future task.
- Finish the `sdf_platform_time_wdt_*` family just enough to make the above possible.

**Non-Goals:**

- Migrating `sdf_match_task` and `sdf_enroll_task` off their inline `#ifndef` +
  `esp_task_wdt_*` blocks onto the wrapper. Desirable, mechanical, and a strictly larger
  diff; it can follow once the wrapper has proven itself on one caller.
- The `sdf_task_bus_attach()` helper that would collapse the subscribe + queue-create +
  watchdog-attach boilerplate shared by the three tasks. That is the refactor this bug is
  evidence *for*, not part of the fix.
- Changing the TWDT timeout, panic behaviour, or idle-core mask configured in
  `sdf_app.c:1638`.
- Bounding the host `admin_action_cb` duration (see Risks).

## Decisions

### Route the admin task through `sdf_platform_time_wdt_*`, not raw `esp_task_wdt_*`

The obvious minimal fix is three lines of `#ifndef CONFIG_IDF_TARGET_LINUX` +
`esp_task_wdt_add/reset/delete` copied from `sdf_services_match.c` — a fourth copy of the
exact pattern whose duplication caused this bug. It would also be completely untestable
on the host, so the regression could recur immediately in a future task.

Instead: add `sdf_platform_time_wdt_add()` and `sdf_platform_time_wdt_delete()` next to
the existing `sdf_platform_time_wdt_reset()`, each carrying the `#ifndef` internally as
that file's existing functions already do. `sdf_services_admin.c` then contains no
`#ifndef` and no `esp_task_wdt.h` include at all — the stale include is deleted rather
than made real.

This deliberately leaves the codebase temporarily inconsistent: admin uses the wrapper,
match and enroll use raw calls. That is the right trade. Converting all three in one
change would mean touching the two files that are *not* broken in order to fix the one
that is, and would make the diff hard to review as a bug fix. The inconsistency is the
visible marker for the follow-on refactor.

### Make the Linux stub observable rather than a no-op

`sdf_platform_time_wdt_reset()` documents itself as "No-op on Linux". For `wdt_add` /
`wdt_delete` a pure no-op would leave the new spec requirement unverifiable. The Linux
implementation therefore tracks registered task handles — a small fixed-size table is
sufficient, since the host suite only ever starts three tasks — and exposes a query for
tests to assert against.

Deliberately *not* proposed: making the host stub simulate watchdog expiry. Detecting a
wedged task on the host would require a timer thread and would make the suite
timing-sensitive. The host assertion covers "is it registered", which is precisely the
defect class here (a missing call, not a mistuned timeout). Whether the reset cadence is
actually fast enough remains an on-target property.

### Reset once per loop iteration, at the top, before the bounded wait

`sdf_services_match.c:346` resets at the top of its loop and comments on why the wait
must stay bounded; `sdf_services_enroll.c:354` resets at the bottom. Admin should follow
the match placement — top of loop, before the `stop_requested` check and the `wait_ms`
computation — so that the reset is not skipped by the `continue` in the action-timeout
branch (`sdf_services_admin.c:295`). That `continue` is the specific reason bottom-of-loop
placement would be wrong here: an admin action timing out repeatedly would skip the reset
every iteration.

## Risks / Trade-offs

**The bootstrap callback was the gating risk. It has been checked, and it clears —
because the mitigation already exists and is currently dead code.**
`sdf_services_try_bootstrap_admin_action()` (`sdf_services.c:330`) runs on the admin task
and, for an unclaimed device receiving a non-ENROLL local action, synchronously invokes
`s_state.config.admin_action_cb` = `sdf_app_on_admin_action()` (`sdf_app.c:433`). Its
longest branch is `SDF_SERVICES_ADMIN_ACTION_FACTORY_RESET` (`sdf_app.c:522`), a
five-step blocking sequence. Against `CONFIG_SDF_PLATFORM_WDT_TIMEOUT_MS=15000`:

| Step | Call | Worst case | Feeds the watchdog while blocked? |
|---|---|---|---|
| 1 | `sdf_storage_erase_all()` | `nvs_flash_erase()` + re-init, sub-second | no |
| 2 | `fp_delete_all_users()` | **12 s** (`CONFIG_SDF_FP_RESPONSE_TIMEOUT_MS=12000`), plus unbounded queueing behind other sensor ops | **yes — every ~1 s** |
| 3 | `sdf_protocol_zigbee_factory_reset()` | 2 × 250 ms bounded lock + non-blocking `esp_zb_*` | no |
| 4 | `sdf_services_reset_state()` | in-memory only | no |
| 5 | `esp_restart()` | — | — |

Step 2 is the only one near the budget, and it is already handled.
`fp_wait_for_reply()` (`fingerprint.c:1138-1145`) blocks in 1 s slices and calls
`esp_task_wdt_reset()` on each slice, with the comment: *"a caller blocked here is exactly
as watchdog-safe as one blocked directly inside a synchronous `fp_*` UART call used to
be."*

That claim is false for the admin task today. `esp_task_wdt_reset()` returns
`ESP_ERR_NOT_FOUND` when the calling task is not subscribed, and the return value is
discarded — so for an unsubscribed caller the entire feeding loop is a silent no-op. The
driver's watchdog safety was written assuming every caller is registered; the admin task
is the one caller for which that assumption does not hold.

This inverts the risk. Registering the admin task is not dangerous *because of* the
fingerprint path — it is what makes the fingerprint driver's already-written mitigation
actually apply to admin-task callers. With steps 1/3/4 all sub-second and step 2
self-feeding, the 15 s budget is comfortable. The gate in tasks 1.1/1.2 does not trigger.

**esp-emu cannot verify any of this, and tasks 5.2/5.3 must be rewritten or dropped.**
Booting the current merged image (`esp-emu` v0.39.0, esp32c6, ESP-IDF v6.0.2) produces a
deterministic panic ~615 ms in, before the services tasks reach steady state, repeating as
an endless boot loop (18+ cycles in 45 s):

```
Guru Meditation Error: Core 0 panic'ed (Load access fault)
MEPC 0x420233ce  RA 0x4202727c  MTVAL 0x00000044  MCAUSE 0x5

0x420233ce  ble_npl_mutex_pend -> ble_hs_lock_nested   (ble_hs.c:288)
0x4202727c  ble_store_read                             (ble_store.c:36)
0x42022e98  ble_store_util_bonded_peers                (ble_store_util.c:133)
              <- sdf_ble_companion.c:1182
```

`MTVAL=0x44` is a dereference of a NULL `ble_hs_mutex`. No BLE/NimBLE log line appears
before the fault: the emulator brings up no BT controller for esp32c6 absent an
`--ble-hci` backend, so `ble_hs` never initializes. `sdf_ble_companion.c:1170-1178`
documents the assumption in a comment — *"This assumes the NimBLE host/bond store is
already initialized by this point"* — and does not guard it. On real hardware the
controller comes up and the assumption holds, so this reads as an emulator-fidelity limit
rather than a shipping bug; it is worth a separate look as a robustness question (the site
is unguarded if `ble_hs` init ever fails), but it is not this change's problem.

Even with that fixed, the emulator models none of the three peripherals whose timing was
actually in question — the fingerprint sensor on UART1, the Zigbee radio, and the BT
controller — so any duration it reported for the factory-reset sequence would be
meaningless. Verification of this change has to be host-suite (registration) plus
on-target (timing).

**Everything else on the admin task is safely short.** The loop body sets state, pulses
an LED and compares timestamps; the idle wait is capped at 1000ms
(`SDF_ADMIN_IDLE_WAIT_CAP_MS`) and every lock acquisition is bounded by
`SDF_SERVICES_LOCK_WAIT_MS` (250ms). The heavyweight `sdf_services_execute_admin_action()`
path — including the blocking `fp_change_user_permission()` UART round-trip — executes on
the *match* task via `sdf_services_try_claim_admin_action()` (`sdf_services_match.c:247`),
not here.

**Adding a fourth registered task tightens the TWDT idle-core budget.** The config at
`sdf_app.c:1638` sets `idle_core_mask` across all cores. Adding a subscriber to a
panicking watchdog is never free; the admin task's 1000ms cap gives a 15× margin, so this
is noted rather than mitigated.

**A stopped-then-restarted task must not double-register.**
`test_sdf_services_start_stop_start_tasks_cycle` exercises exactly this, and it is the
reason `wdt_delete` on the cooperative shutdown path is in scope rather than deferred as
"the task is about to die anyway". `sdf_services_stop_tasks()` can also force-delete a
task, so the Linux stub's bookkeeping must tolerate a handle disappearing without a
matching `wdt_delete`.
