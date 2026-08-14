# Design: Quiesce Poll Loops For Light-Sleep Residency

## Context

`CONFIG_PM_ENABLE=y` and `CONFIG_FREERTOS_USE_TICKLESS_IDLE=y` are both set (`firmware/sdkconfig`), so FreeRTOS's automatic opportunistic light sleep only gets a window as long as the shortest wake cadence among all runnable tasks. That's separate from `sdf_power_task`'s own deliberate `esp_light_sleep_start()` call (unaffected by this change). The task-watchdog timeout is global and set to 5s (`CONFIG_ESP_TASK_WDT_TIMEOUT_S=5`); `sdf_button_task` is not registered with it, `sdf_enroll_task` and `sdf_power_task` are. See proposal.md for the four offending wakeup sources and their cadences.

Tracing the button path found that `iot_button`'s own internal `esp_timer` (`g_button_timer_handle`, `CONFIG_BUTTON_PERIOD_TIME_MS`) drives all press detection/debounce and invokes registered callbacks (`sdf_button_cb`, `sdf_button_single_click_cb`) directly from the `esp_timer` task context — `sdf_button_task`'s own loop is never in that path. Everything else in that task is likewise dead: `sdf_button_isr` is defined but never registered with a GPIO ISR anywhere, `s_button_state.suspended` is written on `POWER_WAKE`/`POWER_SLEEP` but never read, and `sdf_button_task_emit_button_press()`/`sdf_button_task_emit_admin_action_request()` are defined but never called. Searching the whole tree, nothing outside `sdf_services.c`'s task bookkeeping references the task, and the unit tests exercise only `sdf_button_dispatch_action()` (the callback body), never the task.

## Goals / Non-Goals

**Goals:**
- Each remaining task wakes only when there's real work to do (an event, a computed deadline, or a watchdog-safety checkpoint), not on a fixed short interval.
- Button handling stops costing a task, a stack, a queue, and two subscriptions for work it never did.
- `sdf_services_stop_tasks()`'s shutdown latency does not get worse for `sdf_enroll_task` now that it blocks longer between wakeups.
- `sdf_power_task`'s deliberate sleep path (`esp_light_sleep_start()`, checkin-interval wake timer) is untouched.
- `iot_button`'s scan timer stops contributing a ~20ms floor to the automatic-light-sleep window while the enrollment button is idle.

**Non-Goals:**
- Changing `sdf_match_task`'s 400ms poll (out of scope per proposal.md).
- Changing any button *behavior*: gesture-to-action mapping, the admin-fingerprint gate, and the unclaimed-device bootstrap branch all stay exactly as they are. Removing the task is meant to be invisible to a user pressing the button.
- Introducing new Kconfig-tunable intervals; the new constants below stay as `#define`s, matching the existing `SDF_ENROLL_POLL_INTERVAL_MS`-style convention in these files.
- Reconciling the rest of `doc/rtos_tasks.md`'s pre-existing drift (press-mapping table, admin timeout values) beyond the three tasks' trigger/loop-interval/watchdog rows.

## Decisions

### Decision 1: Delete `sdf_button_task` outright rather than convert it to a blocking wait

**Chosen:** Remove the task entirely. Reduce `sdf_services_button.c` to `sdf_button_init()` / `sdf_button_deinit()` (create/register the `iot_button` device and its three callbacks; delete it) plus the retained, unchanged `sdf_button_dispatch_action()`, `sdf_button_resolve_single_click_action()`, `sdf_button_cb`, and `sdf_button_single_click_cb`. Drop `sdf_button_isr`, `s_button_state.suspended`, the two never-called emit helpers, the event queue, and both `POWER_WAKE`/`POWER_SLEEP` subscriptions.

**Rationale:** Once the dead members are removed, nothing is left for the loop to do — press dispatch never went through it, and the only live responsibilities (one-time setup/teardown) don't need a task context. Converting it to a long blocking wait would preserve a task, a 3 KB stack, a queue, and two subscriptions purely to hold a `while` loop that waits for events it no longer does anything with. Deleting it is strictly simpler than the "block forever" alternative and removes a whole task from the scheduler, which is a direct win for the change's own goal.

**Alternatives Considered:**
- Keep the task and just collapse the loop to a blocking wait (the original plan for this change) — rejected once the audit showed *all* of the loop's inputs were dead, not just the poll cadence. A task whose event handling is entirely no-op isn't worth keeping event-driven; it's worth deleting.
- Keep `suspended` and wire it to actually gate presses — rejected: no requirement calls for it, and the device can't receive a button press while in light/deep sleep anyway (GPIO wake resumes the chip first), so the gate would never observably fire.

### Decision 2: Button init/deinit hang off `sdf_services_start_tasks()` / `sdf_services_stop_tasks()`

**Chosen:** Call `sdf_button_init()` from `sdf_services_start_tasks()` and `sdf_button_deinit()` from `sdf_services_stop_tasks()`, rather than moving init to `sdf_services_init()` (where a stale `/* Button is now handled by sdf_button_task */` placeholder comment sits at `sdf_services.c:763`).

**Rationale:** Today the task does both setup and teardown, so `start_tasks`/`stop_tasks` are symmetric — a stop/start cycle currently leaves the button working. Putting init in `sdf_services_init()` while teardown stays in `stop_tasks` would silently break that: the second `start_tasks()` would never recreate the `iot_button` device, leaving the button dead after any stop/start cycle. Keeping both in the start/stop pair preserves the existing lifecycle exactly. The stale placeholder comment gets deleted.

**Alternatives Considered:**
- Init in `sdf_services_init()`, deinit in `sdf_services_stop_tasks()` — rejected for the asymmetry bug described above.
- Init in `sdf_services_init()`, deinit in a new `sdf_services_deinit()` — rejected: no such function exists, and adding one just to host a single call expands the change's surface for no benefit.

### Decision 3: Push shutdown as an event instead of polling `stop_requested` (enroll task only)

**Chosen:** `sdf_services_stop_tasks()` sends a dedicated stop event into `sdf_enroll_task`'s existing event queue (rather than adding a new queue/primitive) so its blocked `xQueueReceive` wakes immediately.

**Rationale:** Reuses the per-task queue and wakeup path the task already has open; no new synchronization primitive to reason about. `stop_requested` in `sdf_services_state_t` stays as the source of truth the task reads once woken (unchanged for `sdf_match_task`/`sdf_admin_task`, which remain out of scope and keep polling it as today). With the button task gone, this applies to `sdf_enroll_task` alone — and `sdf_services_stop_tasks()`'s all-stopped check drops from four task handles to three.

**Alternatives Considered:**
- Task notify (`xTaskNotifyGive`) instead of a queue event — rejected: the task already multiplexes `ENROLLMENT_START`/`POWER_WAKE`/`POWER_SLEEP` through the queue, so adding a second wakeup primitive alongside it buys nothing.

### Decision 4: `sdf_enroll_task` splits into an idle block and a state-driven retry timer

**Chosen:** When `sdf_enrollment_sm_is_active()` is false, block on the event queue with a watchdog-safe cap (1s — 5x margin under the 5s task-WDT timeout, matching the existing "5x poll interval" convention `doc/rtos_tasks.md` already uses for `sdf_match_task`). When an enrollment is active, arm an `esp_timer` one-shot (200ms) that posts a "run next step" event on `SDF_ENROLL_ACT_RETRY_STEP`, and disarm it on `COMPLETE`/`FAIL`.

**Rationale:** Retry cadence should only exist while there's an active state machine to drive; today's unconditional 100ms `vTaskDelay` after every loop iteration runs the same cadence whether or not anything is enrolling. Deriving the retry timer from the state machine's own transitions (rather than a free-running task loop) is exactly the pattern the user asked for ("esp_timer one-shots driven by state transitions").

**Alternatives Considered:**
- Single unified wait (no separate idle/active regimes) — rejected: the two regimes have genuinely different requirements (idle wants to sleep as long as possible modulo watchdog safety; active wants a short, predictable retry cadence for UX), collapsing them would force the idle case to inherit the active case's short cadence or vice versa.

### Decision 5: `sdf_enroll_task`'s boot-readiness wait becomes notify-driven

**Chosen:** Replace `while (!sdf_services_is_ready()) vTaskDelay(10ms)` with a block on a task notification given once by `sdf_services_init()` when initialization completes.

**Rationale:** This is the tightest cadence of the three tasks (100Hz) even though short-lived; it's free to fix alongside the rest of the task and removes a 100Hz busy-loop from the boot path entirely.

**Alternatives Considered:**
- Leave as-is since it's boot-only and short-lived — rejected: trivial to fix while already touching this function, and boot-time light-sleep opportunities matter too (e.g. OTA-adjacent flows that re-init without a full power cycle).

### Decision 6: `sdf_power_task`'s stay-awake wait is deadline-computed with the same 1s watchdog-safe cap

**Chosen:** Compute `min(idle_before_sleep_remaining, wake_guard_remaining, next_battery_report_remaining)`, clamp to the 1s cap (same rationale as Decision 4), and `vTaskDelay` that instead of the fixed `loop_interval_ms` (250ms default).

**Rationale:** Reuses the same watchdog-safety cap value as `sdf_enroll_task` for consistency rather than inventing a second constant. The deadlines are already computed/tracked internally (`sdf_power_policy_evaluate` and friends); this only changes what the loop does with the result.

**Alternatives Considered:**
- Keep `loop_interval_ms` as a Kconfig-tunable poll but just raise its default — rejected: doesn't solve the underlying problem (still wakes even when the nearest deadline is much farther away), and adds a footgun where someone lowers it again later without realizing why it mattered.

### Decision 7: `sdf_power_mark_activity()` also notifies the power task

**Chosen:** Add an `xTaskNotifyGive`-style wake of `sdf_power_task`'s handle inside `sdf_power_mark_activity()`, in addition to its existing timestamp write, and have the task's wait treat "notified" the same as "timeout elapsed" (i.e., just loop back and recompute).

**Rationale:** Without this, deadline-computed waits are still blind to activity that happens *after* the wait was computed — the task would sleep through it and only notice on the next natural wakeup, which could be up to the full 1s cap later. This is a small addition to an existing, narrowly-scoped function and doesn't change its public contract.

**Alternatives Considered:**
- Leave `mark_activity` as a pure timestamp write and accept up to ~1s of staleness — rejected: cheap to fix now, and "activity extends the awake window late" is a real (if minor) behavior gap worth closing while touching this exact code path.

### Decision 8: Flip `enable_power_save` on the enrollment button's `iot_button` config

**Chosen:** Set `button_gpio_config_t.enable_power_save = true` in `sdf_services_button.c`'s `iot_button_new_gpio_device()` call.

**Rationale:** `iot_button.c` already implements the self-stop/resume behavior (`iot_button.c:320`) — this is a one-line config flip, not new logic to write. It removes the ~20ms floor the button scanner otherwise imposes on the automatic-light-sleep window, independent of anything `sdf_button_task` does.

**Alternatives Considered:**
- None seriously considered — the feature exists in the vendored component specifically for this purpose and is off by default in this codebase's usage.

### Decision 9: `sdf_admin_task` is in scope, because it is the binding constraint

**Chosen:** Include `sdf_admin_task` in this change rather than deferring it.

**Rationale:** The automatic-light-sleep window is capped by the *shortest* wake cadence among runnable tasks. `sdf_admin_task` waits `xQueueReceive(..., pdMS_TO_TICKS(100))` unconditionally (`sdf_services_admin.c:117`), so leaving it out pins the window at 100ms no matter what happens to the button, enroll, and power tasks. The rest of the change would then reduce CPU burned *inside* each window without reducing the *number of wake events* — which is the quantity that drives residency. Shipping without it means shipping without the outcome.

It also shares this change's machinery: it needs the same pushed stop signal as `sdf_enroll_task` (Decision 3) and the same deadline-computed wait shape as `sdf_power_task` (Decision 6).

**Alternatives Considered:**
- A separate follow-up change — rejected: it would duplicate the stop-signal plumbing and the wait-cap constant, and would leave this change unable to demonstrate its own success criterion at verification time.
- Also folding in `sdf_match_task` (400ms) — rejected: once `sdf_admin_task` is fixed, `sdf_match_task` becomes the next binding constraint at 400ms, but 400ms is a materially different regime from 100ms, its loop has a genuinely different structure (a 10ms inner wait and a 200ms branch), and it was explicitly not part of the request. Called out in the proposal as the next constraint rather than silently absorbed.

### Decision 10: `sdf_admin_task`'s wait is deadline-computed against the pending-action expiry

**Chosen:** When an admin action is pending, wait until `pending_admin_action_start_us + SDF_ADMIN_ACTION_TIMEOUT_MS`, clamped to `[0, cap]`. When none is pending, wait the full cap. Keep the timeout check itself where it is; only the wait feeding it changes.

**Rationale:** Unlike the button and enroll loops, this task's poll is *not* vestigial — it services a real 10-second timeout (`sdf_services_admin.c:173-194`). But a 10s deadline checked every 100ms is 100x finer than needed. Waiting to the deadline preserves the timeout exactly and removes the cadence.

`sdf_admin_task` is **not** task-watchdog registered — it includes `esp_task_wdt.h` but never calls `esp_task_wdt_add(NULL)`, unlike `sdf_enroll_task` (`:217`) and `sdf_match_task` (`:252`). So its cap is not a watchdog-safety requirement. Apply the same 1s cap anyway, for one reason: a cap is what makes the pushed stop signal a latency optimization rather than a correctness dependency (see the Decision 3 risk). Do not add a watchdog registration as part of this change — that is a separate decision with its own failure-mode implications.

**Alternatives Considered:**
- Block indefinitely when nothing is pending, relying entirely on the pushed stop signal and event wakes — rejected: makes shutdown correctness depend on signal delivery never failing, and `sdf_services_stop_tasks()`'s forced-deletion fallback exists precisely because that assumption is not safe to make.
- Drive the timeout from an `esp_timer` one-shot instead of a computed wait — rejected: it would put the timeout's LED and event-emission work back on the `esp_timer` task, which is the exact hazard `dispatch-admin-actions-off-esp-timer` exists to remove.

### Decision 11: Setting a pending admin action notifies `sdf_admin_task`

**Chosen:** Notify the admin task wherever `pending_admin_action` is set, mirroring Decision 7's treatment of `sdf_power_mark_activity()`.

**Rationale:** A deadline-computed wait is only correct if the task learns about new deadlines. `pending_admin_action` is set from three places, and two of them publish no event that would wake the task: `sdf_services_request_admin_action()` sets it inline (`sdf_services.c:1096`), and the button dispatch path sets it inline. Only the `ADMIN_ACTION_REQUEST` event path wakes the task today. Without a notify, an action set by a non-publishing caller would not begin being tracked until the task's next cap expiry — up to 1s late on a 10s timeout. Tolerable, but easy to close and confusing to leave.

After `dispatch-admin-actions-off-esp-timer` lands, the button path becomes event-driven and this reduces to one non-publishing caller — but the notify is still required for that one, and it makes the invariant hold regardless of how callers evolve.

**Alternatives Considered:**
- Accept up to one cap of latency in starting the countdown — rejected for the same reason as the equivalent alternative in Decision 7: cheap to close while already touching the code.

## Risks / Trade-offs

- **[Risk]** Longer blocking waits in `sdf_enroll_task` mean `sdf_services_stop_tasks()` could, in the worst case (stop signal dropped), take up to the new cap instead of ~100ms to notice. → **Mitigation:** the stop signal is pushed (Decision 3), so the timeout is a backstop, not the normal path; `sdf_services_stop_tasks()`'s existing 13s overall budget already assumes worst-case multi-second waits (it's sized around the ~12s UART timeout), so a 1s cap well under that keeps the existing bound meaningful.
- **[Risk]** Button callbacks run on the shared `esp_timer` task at priority 22, and the work reachable from them is far heavier than a lock acquisition: `sdf_button_resolve_single_click_action()` performs an NVS read via `sdf_services_get_setup_state()` → `sdf_storage_nuki_load()` on every single-click, and on a zero-enrolled-user device `sdf_button_dispatch_action()`'s bootstrap branch calls `admin_action_cb` directly — so a double-click runs a NimBLE advertising restart and an 8-second hold runs `sdf_storage_erase_all()` + `fp_delete_all_users()` (a multi-second blocking UART round trip) + Zigbee factory reset + `esp_restart()`, all on the timer task, starving every other `esp_timer` callback including `iot_button`'s own scan timer. → **Mitigation:** this is pre-existing and *unchanged* by this change — the task never mediated those callbacks, so removing it neither introduces nor worsens it. Flagged here so the removal isn't later misread as having caused it. It is addressed separately by the `dispatch-admin-actions-off-esp-timer` change (with `unify-pending-admin-action-led-mapping` and `centralize-unclaimed-device-bootstrap` as its prerequisites), which is deliberately kept out of this change's scope because moving dispatch across a task boundary alters user-visible timing and introduces a drop-under-backpressure failure mode — a different risk class from quiescing poll loops.
- **[Risk]** Removing a task from `sdf_services_start_tasks()`'s partial-failure rollback ladder (which currently deletes previously created tasks on a later failure) is easy to get subtly wrong while editing. → **Mitigation:** the button task was created *last*, so its removal deletes the final rung rather than reshuffling the middle of the ladder; verify the remaining three-task rollback paths still delete exactly the tasks created before the failure point.
- **[Risk]** `enable_power_save` changes `iot_button`'s internal timing behavior (stop/resume the shared scan timer) for the enrollment button specifically. → **Mitigation:** this button is the only registered `iot_button` device in the firmware (confirmed via search), so there's no other button sharing `g_button_timer_handle` whose timing could regress.
- **[Risk]** The pending-admin-action timeout is now detected within up to 1s of its true expiry rather than within 100ms, and the timeout path does real work on expiry (clears state, flashes red, completes a permission change, emits `ADMIN_ACTION_COMPLETE`). A consumer that assumed tight expiry timing would see it loosen. → **Mitigation:** the timeout is 10s, so 1s is a 10% loosening of a deadline whose purpose is "give the user long enough to present a finger"; nothing in the codebase measures it precisely. Specified explicitly in the delta rather than left as an implementation detail, so the loosening is a stated contract.
- **[Risk]** Missing a `pending_admin_action` write site when adding the notify (Decision 11) leaves that path's timeout countdown starting up to a cap late — a silent, timing-dependent bug. → **Mitigation:** enumerate the write sites during implementation (`sdf_services.c`'s `sdf_services_request_admin_action()`, the button dispatch body, and `sdf_admin_task`'s own `ADMIN_ACTION_REQUEST` handler) and prefer routing the notify through a single setter rather than sprinkling it at each site; `unify-pending-admin-action-led-mapping` and `centralize-unclaimed-device-bootstrap` will already have consolidated much of this code by the time this change lands.
- **[Risk]** Deadline-computed waits in `sdf_power_task` and the state-driven retry timer in `sdf_enroll_task` are new logic paths (vs. today's simpler fixed-interval loops), increasing the surface for off-by-one/edge-case bugs (e.g., negative/zero remaining-time computation after a clock jump). → **Mitigation:** clamp all computed remaining-time values to `[0, cap]` before use; add unit tests for the boundary cases (deadline already passed, deadline exactly at cap, deadline far in the future) alongside the existing `sdf_power`/`sdf_enrollment_sm` test suites.

## Migration Plan

No data migration or persisted-state changes. Rollout is a normal firmware update:
1. Land the button-task removal, the two task-loop changes, and the `enable_power_save` flip together (they're independent of each other but share the same verification pass).
2. Update `doc/rtos_tasks.md`: remove the `sdf_button` row and §2.6, and correct the trigger/loop-interval/watchdog rows for `sdf_power` and `sdf_enroll`.
3. Verify on target hardware (or the Linux host test runner where applicable) that: button presses, enrollment, and sleep/wake all still behave correctly, and that shutdown (`sdf_services_stop_tasks()`, exercised by factory reset) still completes within its existing bound.

Rollback is a plain revert; nothing here is persisted or externally observable in a way that requires a migration step.
