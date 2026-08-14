## 0. Sequencing check

- [x] 0.1 Confirm `unify-pending-admin-action-led-mapping`, `centralize-unclaimed-device-bootstrap`, and `dispatch-admin-actions-off-esp-timer` have all landed — this change is fourth in the chain.
- [x] 0.2 Re-baseline this change's MODIFIED requirement blocks against the live spec. Three are expected to be **droppable** because the earlier changes already reworded them: "Simplified Pre-Enrollment Bootstrap Branch" (by `centralize-unclaimed-device-bootstrap`), "State-Dependent Single-Click Setup Action" and "Double-Press Requests BLE Companion Pairing Window" (by `dispatch-admin-actions-off-esp-timer`). Verify against `openspec/specs/sdf-services-tasks/spec.md` rather than assuming; keep only "Admin-Only Actions Not Bound To Physical Button Gestures", which no earlier change touches.

## 1. Remove `sdf_button_task`

- [x] 1.1 In `sdf_services_button.c`, delete `sdf_button_task()`, `sdf_button_isr()`, the `sdf_button_task_init_subscriptions()`/`_deinit_subscriptions()` pair, and the `sdf_button_task_state_t` members that only served the loop (`event_queue`, `sub_power_wake`, `sub_power_sleep`, `task_handle`, `suspended`) — retaining `btn_handle`.
- [x] 1.1a **Do not** delete the button-press emit helper. It was dead code when this change was written, but `dispatch-admin-actions-off-esp-timer` lands first and makes the producer pattern live — the button callbacks now emit `SDF_EVENT_ROUTER_BUTTON_PRESS` through it. Confirm against the post-`dispatch-admin-actions-off-esp-timer` source which emit helpers survive, and delete only genuinely unreferenced ones.
- [x] 1.2 Add `sdf_button_init()` containing the existing one-time `iot_button_new_gpio_device()` + three `iot_button_register_cb()` calls (single-click, double-click, long-press-8s), unchanged in behavior.
- [x] 1.3 Add `sdf_button_deinit()` containing the existing `iot_button_delete()` teardown.
- [x] 1.4 Confirm `sdf_button_dispatch_action()`, `sdf_button_resolve_single_click_action()`, `sdf_button_cb()`, and `sdf_button_single_click_cb()` are carried over unchanged — `sdf_button_dispatch_action()` must keep its non-static linkage and internal-header declaration, since the host unit tests call it directly.
- [x] 1.5 In `sdf_services_internal.h`, drop the `sdf_button_task` declaration and the `button_task` handle from `sdf_services_state_t`; declare `sdf_button_init`/`sdf_button_deinit`.
- [x] 1.6 In `sdf_services.c`, remove the button `xTaskCreate` and its `SDF_BUTTON_TASK_*` defines; call `sdf_button_init()` from `sdf_services_start_tasks()` and `sdf_button_deinit()` from `sdf_services_stop_tasks()` (per design Decision 2, preserving stop/start symmetry).
- [x] 1.7 Update `sdf_services_start_tasks()`'s partial-failure rollback ladder for three tasks, and its "All 4 services tasks started" log message.
- [x] 1.8 Update `sdf_services_stop_tasks()`: drop `button_task` from the all-stopped check and the forced-deletion fallback, and update the comment enumerating which tasks poll `stop_requested`.
- [x] 1.9 Delete the now-stale `/* Button is now handled by sdf_button_task */` placeholder comment in `sdf_services_init()`.
- [x] 1.10 Confirm the existing button tests in `test_sdf_services.c` still pass untouched (they only exercise `sdf_button_dispatch_action()`); add a test that a stop/start cycle leaves button handling functional.

## 2. Shutdown-signal plumbing (`sdf_enroll_task`, `sdf_admin_task`)

- [x] 2.1 Add a stop/shutdown event (or reuse an existing sentinel) that `sdf_services_stop_tasks()` can push into a task's event queue, distinct from the events those tasks already handle.
- [x] 2.2 Update `sdf_services_stop_tasks()` to send that signal to **both** `sdf_enroll_task` and `sdf_admin_task` after setting `stop_requested`, so their blocked `xQueueReceive` calls wake immediately instead of waiting out their caps.
- [x] 2.3 Confirm the signal is a latency optimization only — both tasks must still observe `stop_requested` at their next cap expiry if the signal is dropped, keeping shutdown correct within the existing 13s budget.
- [x] 2.4 Verify `sdf_match_task` is unaffected (it remains polling `stop_requested` as today, out of scope for this change).

## 3. Enrollment button: quiesce `iot_button`'s own scanner

- [x] 3.1 Set `button_gpio_config_t.enable_power_save = true` in `sdf_services_button.c`'s `iot_button_new_gpio_device()` call.
- [x] 3.2 Verify (via emulator/hardware run) that single/double/long-press classification still works correctly with power-save enabled — the scan timer stopping/resuming must not change debounce or multi-press-window behavior.

## 4. `sdf_enroll_task`: idle block + state-driven retry timer

- [x] 4.1 Replace the boot-readiness busy-wait (`while (!sdf_services_is_ready()) vTaskDelay(10ms)`) with a block on a task notification given once by `sdf_services_init()` when initialization completes.
- [x] 4.2 Define the watchdog-safe idle cap (1s) as a named constant alongside the existing `SDF_ENROLL_POLL_INTERVAL_MS`-style defines.
- [x] 4.3 When `sdf_enrollment_sm_is_active()` is false, block on the event queue with the watchdog-safe cap instead of `xQueueReceive(100ms)` + unconditional `vTaskDelay(100ms)`; reset the task watchdog on each wake (event or cap timeout).
- [x] 4.4 Add an `esp_timer` one-shot (200ms, named constant) that, while active, posts a "run next step" event on `SDF_ENROLL_ACT_RETRY_STEP`; arm it when a retry is scheduled, disarm it on `COMPLETE`/`FAIL`.
- [x] 4.5 Ensure the watchdog is still reset promptly during an active enrollment (the existing per-call-site reset inside the blocking UART round trip, noted in the current code's comments, is unaffected by this change — confirm it still applies with the new control flow).
- [x] 4.6 Add/update unit tests: idle regime doesn't poll faster than the watchdog-safe cap; active regime schedules the next step via the one-shot timer and stops scheduling after completion/failure; boot-readiness wait unblocks on the init notification.

## 4a. `sdf_admin_task`: deadline-computed wait

- [x] 4a.1 Reuse the shared 1s cap constant for `sdf_admin_task`. Note it is **not** a watchdog-safety cap here — this task is not watchdog-registered (it includes `esp_task_wdt.h` but never calls `esp_task_wdt_add(NULL)`); the cap exists so the pushed stop signal stays an optimization rather than a correctness dependency (design Decision 10).
- [x] 4a.2 Replace the fixed `xQueueReceive(..., pdMS_TO_TICKS(100))` (`sdf_services_admin.c:117`) with a computed wait: when an admin action is pending, `pending_admin_action_start_us + SDF_ADMIN_ACTION_TIMEOUT_MS` minus now, clamped to `[0, cap]`; when none is pending, the full cap.
- [x] 4a.3 Leave the timeout-check block (`:173-194`) and its behavior on expiry — clear state, `led_flash_red()`, `sdf_services_complete_permission_change()` for `CHANGE_PERMISSION`, `sdf_admin_task_emit_action_complete()` — unchanged. Only the wait feeding it changes.
- [x] 4a.4 Do **not** add a task-watchdog registration to this task; that is a separate decision (design Decision 10).
- [x] 4a.5 Enumerate every site that sets `pending_admin_action` and add a task notification so the admin task recomputes its wait (design Decision 11). Prefer routing the notify through a single setter over sprinkling it at each site — by this point `unify-pending-admin-action-led-mapping` and `centralize-unclaimed-device-bootstrap` will have consolidated much of that code.
- [x] 4a.6 Have the task treat "notified" identically to "wait elapsed": loop back and recompute, matching `sdf_power_task`'s treatment in 5.3.
- [x] 4a.7 Add/update unit tests: computed wait targets the pending action's expiry; clamps when already expired and when no action is pending; a pending action set by a non-publishing caller wakes a waiting task; the 10s timeout still fires and still produces its full expiry side effects.

## 5. `sdf_power_task`: deadline-computed stay-awake wait

- [x] 5.1 Reuse the 1s watchdog-safe cap constant (or define an equivalent local one) for `sdf_power_task`.
- [x] 5.2 Compute the stay-awake wait as `min(idle_before_sleep_remaining, wake_guard_remaining, next_battery_report_remaining)`, clamped to `[0, cap]`, and use it in place of the fixed `vTaskDelay(loop_interval_ms)` for the `STAY_AWAKE` branch.
- [x] 5.3 Add an `xTaskNotifyGive`-style wake of `sdf_power_task`'s handle inside `sdf_power_mark_activity()`; have the task's wait treat a notification the same as a timeout (loop back and recompute).
- [x] 5.4 Confirm the `SLEEP_LIGHT`/`SLEEP_DEEP` branches and the underlying `esp_light_sleep_start()`/checkin-interval mechanism are untouched by this change.
- [x] 5.5 Add/update unit tests: computed wait matches the nearest of the three deadlines; clamps correctly when a deadline has already passed or is farther out than the cap; `sdf_power_mark_activity()` wakes a waiting task early (host-side test using the mock platform).

## 6. Documentation

- [x] 6.1 Update `doc/rtos_tasks.md`'s `sdf_power` row/section: trigger, loop interval, and watchdog description to reflect the deadline-computed wait and the 1s cap.
- [x] 6.2 Update `doc/rtos_tasks.md`'s `sdf_enroll` row/section: trigger and watchdog description to reflect the idle block + state-driven retry timer.
- [x] 6.2a Update `doc/rtos_tasks.md`'s `sdf_admin` row/section: trigger and loop-interval to reflect the deadline-computed wait and the 1s cap.
- [x] 6.2b Correct the §5 watchdog-assignment table's `sdf_admin` row — it claims a 15s task watchdog, but the task is not watchdog-registered at all. Pre-existing drift, fixed here because this change is what makes the row misleading in a way that matters.
- [x] 6.3 Remove `sdf_button` from `doc/rtos_tasks.md`'s task table and delete its §2.6 section, replacing it with a short note that button handling is taskless (driven by `iot_button`'s own scan timer and callbacks, initialized/torn down by `sdf_services_start_tasks()`/`sdf_services_stop_tasks()`); update the "Total Stack RAM" figure and the §5 watchdog-assignment table accordingly.

## 7. Verification

- [x] 7.1 Run the full host test suite (`sdf_services`, `sdf_power`, `sdf_state_machines`) and confirm no regressions.
- [x] 7.2 Run/emulate an end-to-end pass: single-click, double-click, and 8s-hold each still dispatch their correct action with the button task gone; then enrollment → sleep/wake → factory-reset shutdown, confirming behavior is unchanged from a user-observable standpoint.
- [x] 7.2a Verify the admin-action timeout end to end: set a pending action, let it expire, confirm the red flash, the `ADMIN_ACTION_COMPLETE` emission with `ESP_ERR_TIMEOUT`, and — for `CHANGE_PERMISSION` — the permission-change completion, all still occur, within ~1s of the 10s mark.
- [x] 7.2b Verify shutdown latency: call `sdf_services_stop_tasks()` while both `sdf_enroll_task` and `sdf_admin_task` are idle and blocked, and confirm both clear their handles promptly rather than at cap expiry.
- [x] 7.3 If hardware or a suitable emulator is available, spot-check light-sleep residency qualitatively (e.g. via power-management logging or a scope) to confirm the automatic-light-sleep window is no longer capped at ~20-100ms during idle periods. **This is the change's success criterion** — with `sdf_admin_task` now in scope, the expected new cap is `sdf_match_task`'s 400ms, not 100ms. If the observed window is still ~100ms, something in this change did not take effect.

