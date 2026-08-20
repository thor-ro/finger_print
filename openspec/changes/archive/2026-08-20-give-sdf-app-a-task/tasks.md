## 0. Probe: size the app task's stack

Runs **first, against current `main`** (design.md — D8). Its output is an input to group 1, not a review of it. Produces a measurement and a stack size; the instrumentation itself is reverted before group 1 starts.

- [x] 0.1 Add temporary instrumentation logging `uxTaskGetStackHighWaterMark(NULL)` at the end of each `sdf_event_router_dispatch()` call, tagged with the dispatched event type. Mark the block clearly as probe-only.
- [x] 0.2 Boot under `esp-emu` (esp32c6) and record the baseline high-water mark after boot settles, with only ordinary events dispatched. **Baseline: 2448 bytes free of the router task's 3072**, i.e. 624 bytes deepest use. Only three dispatches occur during an ordinary boot, all `POWER_BATTERY` (type 5) at `PRIO_LOW`: 2692 free at 616 ms, then 2448 free at 646 ms and again at 896 ms, stable thereafter. Note the `AUDIT` events `sdf_app_init()` emits for the storage-policy result never reach dispatch at all — they are emitted before `sdf_event_router_init()` runs, so `emit()` rejects them with `ESP_ERR_INVALID_ARG`. The baseline is therefore a genuinely shallow path.
- [x] 0.3 Add temporary `main`-task instrumentation that calls `sdf_services_set_web_reg_auth()` with a recognizable username, then emits `WEB_REG_AUTH_RESULT` with `authorized = true`. `false` skips PBKDF2 and measures nothing. Username `probe_stack_user`. Two things the design did not anticipate were needed:
      - **The NVS-write leg does not run on a fresh device.** `sdf_app_on_web_reg_auth_result()` persists only into an index where `sdf_storage_web_user_load(i, &existing) == ESP_OK && !existing.valid`, but on a device that has never saved a web user `load()` returns `ESP_ERR_NVS_NOT_FOUND` for every index, so the loop finds no slot and the save is silently skipped. The first run measured PBKDF2 only (1636 free). The probe now seeds index 0 with a `valid = false` blob first, which makes the write leg execute (`Saved web user at index 0`). **This is a real defect in the handler, not a probe artifact** — see 5.3(e).
      - **The `main` task stops being scheduled under `esp-emu` after roughly 2.6 s.** Once every task is blocked the emulator fast-forwards to the next `esp_timer` alarm (observed: emulated time jumps 6946 ms straight to 60896 ms), and `vTaskDelay()` expiries in between are skipped. A probe that waits ~8 s for boot to settle never fires. The probe therefore triggers at 1646 ms, inside the window where the `main` task still runs. This does not affect the measurement: the dispatch task's high-water mark is monotonic and had already stabilised at its baseline by 896 ms.
- [x] 0.4 Run and record the high-water mark for that dispatch. **Deep path: 1544 bytes free of 3072**, i.e. 1528 bytes deepest use, logged as `PROBE hwm after dispatch type=16 prio=1 free=1544` at 2136 ms, immediately after `sdf_app: Saved web user at index 0`. The dispatch took ~490 ms of emulated time (1646 ms -> 2136 ms), consistent with a 10,000-iteration PBKDF2 plus an NVS write. **Lower bound, as predicted**: `sdf_ble_companion_reply_auth()` scans `s_connections` for a connected peer with `auth_pending` and returns `ESP_ERR_NOT_FOUND` at `sdf_ble_companion.c:1494` when none is found; under the emulator no central can connect (allow-list-filtered advertising, empty allow list), so `sdf_ble_companion_set_authenticated()` and its `ble_gatts_notify_custom()` never execute and their frames are absent from the figure.
- [x] 0.5 Record baseline, deep-path figure, and the delta in this file. Any emulator panic is a real defect, not a fidelity artifact — trace it before dismissing it.

      | | high-water free | deepest use (of 3072) |
      |---|---|---|
      | Baseline (`POWER_BATTERY`, `PRIO_LOW`) | 2448 B | 624 B |
      | Deep path (`WEB_REG_AUTH_RESULT`, `PRIO_HIGH`, PBKDF2 + NVS save) | 1544 B | 1528 B |
      | **Delta** | **904 B** | |

      Measured on `esp-emu` v0.39.0, esp32c6, ESP-IDF v6.0.2, against current `main` (831bf63) with only the probe applied. **No panic, no abort, no watchdog trip** in any of the six runs; the three `Panic intercept:` lines in each log are esp-emu registering symbol interception at load, not panics.
- [x] 0.6 Derive `SDF_APP_TASK_STACK` from the deep-path figure plus the margin the project applies elsewhere (compare against how `sdf_zigbee` at 8 KB and `sdf_match` at 6 KB were set). State the derivation, not just the number. **`SDF_APP_TASK_STACK` = 4096.**

      Correction to the task's premise: read from the `xTaskCreate()` call sites, `sdf_zigbee` is **6144**, not 8 KB, and `sdf_match` is **4096**, not 6 KB. The comparison below uses the actual figures.

      Derivation: the measured deep-path depth is **1528 B**, and it is a lower bound (0.4). The frames it omits are `sdf_ble_companion_set_authenticated()` plus a single-byte `ble_gatts_notify_custom()` — shallow, a few hundred bytes at most, since the notify path allocates an mbuf rather than recursing. The measurement also already includes the dispatch task's own loop frames (`sdf_event_router_task` -> `sdf_event_router_dispatch` -> `slot->cb`), which the app task's loop frames replace at comparable depth, so it transfers directly.

      4096 gives 2.7x headroom over the measured figure. That is the size the project already gives `sdf_match`, `sdf_enroll`, `sdf_admin` and `fp_owner` — the four tasks whose work is closest in kind (NVS access, crypto, driver I/O) and the three the app task is explicitly modelled on (design.md — D2). 3072 would leave only 1.5 KB of margin over a figure that is known to be incomplete and that a compiler or IDF-version change could move; 6144 is what `sdf_zigbee` needs for a closed-source radio stack and buys nothing here. Sizing at the established tier rather than at the measurement plus an invented constant also keeps the table in `doc/rtos_tasks.md` internally consistent.
- [x] 0.7 Call `sdf_services_clear_web_reg_auth()` in the probe and clear the bogus persisted web user. Both are in the probe. Note that neither executed under the emulator and neither needed to: the `main` task stops being scheduled at ~2.6 s (0.3), so the cleanup block after the 500 ms settle delay never ran — and the handler itself calls `sdf_services_clear_web_reg_auth()` as its own last statement, so the pending-auth state was cleared regardless. Nothing leaked to disk either: the emulator was run without `--save-state`, so its flash image is discarded on exit and `probe_stack_user` exists only inside that run. Confirmed by checksum: the merged image is byte-identical before and after each run.
- [x] 0.8 Revert all instrumentation from 0.1 and 0.3. Confirm with `rtk git diff` against the merge base that nothing probe-related remains — the measurement is the deliverable, the instrumentation is not.

## 1. Give `sdf_app` a task

- [x] 1.1 Add `SDF_APP_TASK_STACK` (from 0.6), `SDF_APP_TASK_PRIORITY` (5 — the app task owns the lock-actuation path; matches `sdf_match` and `sdf_admin`), and `SDF_APP_EVENT_QUEUE_DEPTH` (10, matching the three service task queues) with a comment giving the reasoning for each, not just the value.
- [x] 1.2 Add the app event queue holding `sdf_event_router_event_t` by value, and a task handle, to `sdf_app.c`'s static state.
- [x] 1.3 Write the trampoline callback: copy the event, `xQueueSendToFront()` when `event->priority == SDF_EVENT_ROUTER_PRIO_CRITICAL` and `xQueueSendToBack()` otherwise, both with timeout `0`. Comment the zero timeout as load-bearing (design.md — D5) so a future reader does not "fix" it.
- [x] 1.4 On enqueue failure, `ESP_LOGW` with the dropped event's type and increment a new `s_app_evt_dropped` counter. Place it alongside the existing `s_app_audit_err_*` counters (`sdf_app.c:57-60`) so it surfaces wherever those do.
- [x] 1.5 Write the task body: bounded wait on the queue (cap `SDF_APP_IDLE_WAIT_CAP_MS` = 1000, matching `SDF_ADMIN_IDLE_WAIT_CAP_MS`), `sdf_platform_time_wdt_reset()` each iteration, switch on `event->type` and call the existing handler bodies.
- [x] 1.6 `sdf_platform_time_wdt_add()` as the task's first action. There is no stop path, so no `sdf_platform_time_wdt_delete()` — the spec records this as a scoped exception; do not add a shutdown path to satisfy symmetry.
- [x] 1.7 Repoint all nine `sdf_event_router_subscribe()` calls (`sdf_app.c:1717-1798`) to the trampoline. Keep the `min_prio` argument of every subscription exactly as it is — the `SECURITY_LOCKOUT` subscription in particular relies on `PRIO_NORMAL` admitting both critical and normal events, and the comment above it at `sdf_app.c:1724` explains why.
- [x] 1.8 Convert `sdf_app_on_event()`, `sdf_app_on_web_reg_auth_result()` and `sdf_app_on_admin_action_complete()` from subscriber callbacks into task-side handlers. Logic unchanged.
- [x] 1.9 Create the task after the last subscription and before `sdf_event_router_start()` (`sdf_app.c:1840`). Update the comment there — it asserts the 9/4/3/3/2 subscription split, which is still correct and worth keeping accurate.
- [x] 1.10 Audit the three moved handler bodies for anything that depended on running under the dispatch task — incidental serialization against other subscribers, reentrancy assumptions, statics shared with another subscriber. The alarm mask (group 3) is one known case; record what else was checked, **including negative results**, rather than only what was found.

      Every static and cross-module call reachable from the three moved handlers was classified by what else touches it and from which task.

      **Found.** `s_zigbee_alarm_mask` via `sdf_app_set_alarm_mask_bits()` — a read-modify-write that now runs from a second task alongside the Zigbee-side writers. Tracked in group 3; nothing else in the audit turned up a second instance.

      **Negative results** (checked, unchanged by the move):

      1. `s_app_audit_err_biometric_failed` / `_auth_lockout` / `_nonce_replay` / `_protocol` (`sdf_app.c:81-84`) and the new `s_app_evt_dropped`: written only from the `AUDIT` case of `sdf_app_on_event()`. One writer before (router task), one writer after (app task). Nothing outside that switch case touches them.
      2. `s_has_creds` / `s_pairing_active`: read-only in the moved code. Written by `sdf_app_init()` (main task, before the app task exists), the Nuki pairing path and `sdf_app_on_ble_ready()` / `sdf_app_check_pairing_complete()` (NimBLE host task), and `sdf_app_power_wakeup()` (power task). The read was already unsynchronised against three other tasks; the move exchanges one reader task for another and adds no writer.
      3. `s_ble_admin_action_pending` / `s_ble_admin_action` / `s_ble_admin_action_conn_handle`: written by `sdf_app_on_ble_admin_action_request()` (NimBLE host task), by `sdf_app_on_admin_action()` (admin task), and by the moved `sdf_app_on_admin_action_complete()`. The moved handler was never serialised against the other two — before the change its context was the router task, which is no more privileged than the app task. Same race class, same guard (`pending && action matches`), no change.
      4. Lock state (`s_lock_flow`, `s_lock_action_pending`, `s_latch_sequence_active`), reached from `sdf_app_on_event()` -> `sdf_app_lock_action()`: already reached from the NimBLE host task (`sdf_app_on_message`, `sdf_app_lf_on_complete`, `sdf_app_on_ble_ready`), the power task (`sdf_app_power_busy`, `sdf_app_power_wakeup`) and the main task (`sdf_app_init`). The router task was one of four contexts; the app task is the same one of four. `sdf_app_lock_action()` is exported in `sdf_app.h:14` but has no caller outside `sdf_app.c` today, so no other component widens the set. Pre-existing, unchanged in kind, and out of scope here — recorded under 5.3.
      5. Incidental serialisation against other subscribers: `sdf_app` shares an event type with another subscriber in exactly two places — `ENROLLMENT_COMPLETE` and `ENROLLMENT_FAILED`, also taken by `sdf_ble_companion` (`sdf_ble_companion.c:1351,1359`). Those two handlers build a cJSON payload and notify authenticated connections; they touch none of the state the app handlers touch, and neither side consumes a result the other produces. Running out of lock-step with them is harmless. The remaining seven app subscriptions have no co-subscriber at all.
      6. Cross-module state reached from the moved handlers is lock-protected, so it never relied on dispatch-task serialisation: the pending web-registration request sits behind `s_state.lock` (`sdf_services.c:1429`, `:1456`, `:1468`), and the companion's connection table behind `s_lock` (`sdf_ble_companion_reply_auth()` at `sdf_ble_companion.c:1471`, `sdf_ble_companion_reply_admin_action()` at `:1500`). The app task holds no lock of its own across either call, so the added hop introduces no lock-ordering edge.
      7. Reentrancy: the three handlers still cannot overlap each other — one task, one queue, one event in flight. Previously that came from the router task being single-threaded; the guarantee is preserved, not weakened, and no handler was relying on anything stronger.
      8. Ordering between the two handlers that resolve the same pending web-registration request: `WEB_REG_AUTH_RESULT` is emitted `PRIO_HIGH` from the admin task (`sdf_services.c:466`) and `ADMIN_ACTION_COMPLETE` also `PRIO_HIGH` (`sdf_services_admin.c:120`). Neither is `PRIO_CRITICAL`, so both take the back of both queues and FIFO order survives the second hop — whichever runs second still finds the request already cleared, as before. Front-insertion applies only to `PRIO_CRITICAL`, whose sole producer in the tree is `sdf_services_match.c:226` (lockout entered), so two criticals cannot reorder against each other either.
      9. Blocking: no moved handler waits on anything the app task itself must produce, so the new queue cannot deadlock against its own handlers. The only blocking primitive in `sdf_app.c` is the app task's own `xQueueReceive()`; `sdf_app_lock_action()` returns immediately (it either queues via `sdf_app_queue_lock_action()` or starts the async lock flow whose completions arrive on the NimBLE host task). The handlers *can* block on work outside the file — NVS in `sdf_storage_web_user_save()`, `s_lock` with `portMAX_DELAY` in `sdf_ble_companion_reply_auth()` — which is the reason for the move, not an obstacle to it: that blocking now stalls the app task instead of the router.
- [x] 1.11 Confirm `sdf_event_router_capacity.h` needs no edit: nine subscriptions in, nine out. The boot line should still read `Event router started: 21/21 subscribers registered`. Assert on that line rather than reasoning about it.

      Asserted, not reasoned about. Built the target image, merged it (`esptool merge-bin`, SHA over the same four artifacts the build emits), and booted it under `esp-emu` v0.39.0 / esp32c6 for 12 s:

      ```
      I (616) sdf_event_router: Event router started: 21/21 subscribers registered
      ```

      Unchanged, so `sdf_event_router_capacity.h` needs no edit. The boot is otherwise clean: no panic, no abort, no watchdog trip, no `App queue full` line. The three `E (...)` lines in the log are the known emulator gaps (`nvs_sec_provider` HMAC eFuse, `gpio_install_isr_service` already installed, fingerprint sensor absent), all present before this change.
- [x] 1.12 Host test: an event dispatched to an app subscription is handled on the app task, not the dispatch task.

      `test_sdf_app_task_event_handled_on_app_task` in `components/sdf_app/test/test_sdf_app_task.c`. Emits a probe event through the real router and asserts the handling task is the app task, is not the caller, and that the trampoline itself ran on a different task (the router's dispatch task) — so both hops are observed, not just the endpoint.
- [x] 1.13 Host test: with the app queue full, the trampoline returns without blocking and the drop is counted.

      `test_sdf_app_task_full_queue_trampoline_does_not_block`. Suspends the app task, fills the queue to `SDF_APP_EVENT_QUEUE_DEPTH`, times the overflowing call (asserts it returns in <10 ms, i.e. it did not sit on the queue's send timeout), asserts the drop counter incremented by exactly 1, then resumes and asserts the dropped tag never arrives.
- [x] 1.14 Host test: a critical-priority event enqueued behind lower-priority ones is handled first.

      `test_sdf_app_task_critical_event_handled_first`. Four NORMAL events then one CRITICAL, all while the app task is suspended; asserts the CRITICAL tag is handled first and the four NORMAL ones keep their arrival order behind it (D4).
- [x] 1.15 Host test: the app task is watchdog-registered while running.

      `test_sdf_app_task_is_watchdog_registered`. Polls `esp_task_wdt_status(app_task)` for up to 2 s expecting `ESP_OK`, since the task subscribes on its first loop iteration rather than at create time.
- [x] 1.16 `rtk idf.py build` for the target and the host test runner; host test suite green. Both before moving on.

      Both build. Host suite green: **310 Tests 0 Failures 11 Ignored**. Target runner under `esp-emu` (esp32c6): **282 Tests 8 Failures 11 Ignored**, with all 21 `sdf_app`/`test_lock_flow` tests passing — the 6 new ones from 1.12-1.15 and 3.5 plus the 15 previously-orphaned ones. See the note below for what the 8 failures are.

      **1.12-1.16 and 3.5 were blocked on the same gap — `sdf_app` had no test target at all. Resolved via option 2 (repair the target test runner), chosen by the change owner.**

      What was originally blocking:
      - `firmware/test_runner/main/CMakeLists.txt` appends `sdf_app` to `test_reqs` only `if(NOT IDF_TARGET STREQUAL "linux")`, so the host runner cannot link it. `AGENTS.md:37` records this deliberately: `sdf_app` and the lock-flow suites are hardware-only because they pull in `sdf_ble_companion`/BLE/WiFi/OTA, and the named follow-up is `add-linux-target-sdf-app-support`.
      - `components/sdf_app/test/test_sdf_app.c` and `test_lock_flow.c` were not referenced by any `CMakeLists.txt` in the tree, on either target — orphaned, not merely linux-excluded.
      - The documented hardware path (`idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.hw.defaults" set-target esp32c6`) did not configure at all: `Failed to resolve component 'led_strip' required by component 'sdf_drivers'`.

      What was done to unblock (all confined to the test runner; no production behaviour changed):
      - Added `firmware/test_runner/main/idf_component.yml` declaring the same managed components `firmware/main` uses (`esp-zigbee-lib`, `esp-zboss-lib`, `led_strip`, `button`, `cjson`), each gated `if: "target != linux"`. This is what fixed the `led_strip` configure failure.
      - `firmware/test_runner/CMakeLists.txt`: added `sdf_app` and `sdf_ble_companion` to `EXTRA_COMPONENT_DIRS` and to `COMPONENTS` on non-linux targets; added `include(../cmake/version.cmake)` before `project()`, which the main firmware project already does — without it `sdf_ota` links `${SDF_VERSION_C}` as an empty string and `sdf_ota_version_string` is undefined.
      - `firmware/test_runner/main/CMakeLists.txt`: split the source/require lists per target. The chip target now compiles `test_sdf_app.c`, `test_sdf_app_task.c` and `test_lock_flow.c` and defines `SDF_APP_TESTING`; the four suites that drive linux-only mock seams (fingerprint owner task, event router, zigbee protocol, GATT scratch) stay on the linux side.
      - `test_runner_main.c`: `#ifdef CONFIG_IDF_TARGET_LINUX` around those four suites' `RUN_TEST` blocks, and a matching `#ifndef` block running the 21 target-only tests.
      - `sdkconfig.hw.defaults`: enabled NimBLE peripheral/GATT server/GAP service (the runner now links `sdf_ble_companion`, which registers a GATT service); mirrored the host runner's test-only overrides `CONFIG_SDF_CONFIG_ENABLE_RUNTIME_OVERRIDE=y` and `CONFIG_SDF_CLI_ENABLE=y`; raised `CONFIG_ESP_MAIN_TASK_STACK_SIZE` to 98304 (the Nuki replay-window suites hold arrays of whole 2 KB messages on the main task stack — the oversized-window case alone needs ~43 KB of frame, and at 16 KB it faulted with a `Store access fault` inside the panic handler).

      **The remaining target failures are pre-existing and none are caused by this change** — these suites had never been run on a chip target before, because the target runner did not configure. They split into two groups:
      - *Host-environment assumptions (4, still failing):* `test_sdf_platform_sleep_retention_linux_noops` (asserts the linux no-op stubs; the target does real retention), `test_sdf_platform_time_wdt_registration_lifecycle` and `test_sdf_platform_time_wdt_not_found_one_shot_diagnostic` (assert against the linux mock WDT), `test_sdf_services_start_stop_start_tasks_cycle` (the emulator has no fingerprint sensor).
      - *A genuine production defect (4), since fixed:* the four `test_sdf_ota_version_compare_*` failures. `sdf_ota_version_compare()` was **defined twice** — `components/sdf_ota/src/sdf_ota_version.c` (full semver: `strtol`, uppercase `V`, lexicographic pre-release compare) and `components/sdf_ota/src/sdf_ota.c` (git-describe oriented: `sscanf`, lowercase `v` only, pre-release ordering inverted, commit-count compare). On `IDF_TARGET=linux` only `sdf_ota_version.c` is compiled, so the host suite had always tested the copy the device does **not** run; on a chip target the linker resolves `sdf_ota.c`'s copy and the OTA version gate behaved differently. Each of the four failures was explained exactly by that copy: `"V1.2.3"` unrecognised (→ parsed as 0.0.0), `"1.0.0-beta"` vs `"1.0.0"` returning OLDER where semver says NEWER, `-alpha`/`-beta` comparing EQUAL, and unparseable input comparing as 0.0.0 instead of returning EQUAL.

        Fixed at the change owner's request, outside this change's scope: the `sdf_ota.c` copy is deleted, leaving the tested semver one as the single definition. The only behaviour that copy had and the semver one lacked — ordering `git describe`'s `<tag>-<commits>-g<hash>` pre-release by commit count rather than lexically, which matters because those are the strings the OTA gate actually compares — was folded into `compare_pre_release()` and pinned by a new test, `test_sdf_ota_version_compare_git_describe_orders_by_commit_count`. Target now **283 Tests 4 Failures 11 Ignored** (only the host-environment group above remains); host **311 Tests 0 Failures 11 Ignored**; `firmware/` production build unaffected.


## 2. Ban emit from dispatch

Must land **after** group 1 — installing the guard first logs an error on every biometric match in the interval (design.md — Migration Plan).

- [x] 2.1 Add `bool in_dispatch` to the router's static state; set it around the `sdf_event_router_dispatch()` call in `sdf_event_router_task()` and clear it after.
- [x] 2.2 In `sdf_event_router_emit()`, reject with `ESP_ERR_INVALID_STATE` when `s_state.in_dispatch && xTaskGetCurrentTaskHandle() == s_state.task`. Both conjuncts are required — the bool alone rejects legitimate emits from producer tasks that happen to run during a dispatch (design.md — D6).
- [x] 2.3 `ESP_LOGE` the rejection with event type and priority, so a violation is identifiable from a boot or emulator log without a debugger.
- [x] 2.4 Rewrite the `send_timeout_ms` doc comment in `sdf_event_router.h`: it explains ISR and timer contexts but says nothing about dispatch. State that emitting from a subscriber callback is rejected outright, and that a subscriber needing to produce a follow-on event hands off to a task it owns.
- [x] 2.5 Host test: a subscriber whose callback calls `sdf_event_router_emit()` gets `ESP_ERR_INVALID_STATE`, at `PRIO_CRITICAL` and at a non-critical priority, with timeout `0` and with a non-zero timeout.
- [x] 2.6 Host test: an emit from a non-dispatch task succeeds while a dispatch is in progress. This is the regression test for getting 2.2's condition wrong — write it before trusting 2.2.
- [x] 2.7 Host test: with the queue full, a callback's emit attempt returns promptly rather than after the send timeout. Assert on elapsed time, since the return code alone would pass even with the old blocking behaviour.
- [x] 2.8 Update the existing host test asserting re-entrant critical emit is safe to assert rejection instead (see `specs/sdf-event-router/spec.md`).
- [x] 2.9 Boot under `esp-emu` and confirm **no** rejection is logged. A rejection here means group 1 missed a call site; the spec scenario "No subscriber relies on the rejected behaviour" is what this checks.

      Verified two ways, because the emulator alone is not sufficient evidence here.

      **Boot:** `esp-emu` v0.39.0 / esp32c6, 15 s and 45 s runs — zero `Rejecting emit from subscriber callback` lines, `Event router started: 21/21 subscribers registered` unchanged, no panic or watchdog trip. What this does *not* show: emulated time stalls at ~6946 ms once every task blocks (group 0), so no application event is dispatched in the window. The boot run rules out a rejection on the boot path only.

      **Static, which is the load-bearing check:** every callback registered with the router was read for an `sdf_event_router_emit()` call. All five are emit-free — `sdf_app_event_trampoline` (new, `xQueueSendToFront`/`Back`), `sdf_match_task_event_cb`, `sdf_enroll_task_event_cb`, `sdf_admin_task_event_cb` (all `xQueueSend(state->event_queue, &evt_copy, 0)`), and `sdf_ble_companion`'s two enrollment handlers (cJSON payload plus a GATT notify, no emit). That is the whole subscriber set, so no call site can reach the new rejection.

## 3. Synchronize the Zigbee alarm mask

Independent of groups 1 and 2; can land in any order relative to them.

- [x] 3.1 Make `s_zigbee_alarm_mask` (`sdf_app.c:114`) `_Atomic uint16_t`.
- [x] 3.2 Rewrite `sdf_app_set_alarm_mask_bits()` as the compare-exchange loop in design.md — D7, keeping the `new == old` suppression *inside* the loop on the same load that composed `new`.
- [x] 3.3 Call `sdf_protocol_zigbee_update_alarm_mask()` after the CAS succeeds, not under it, and leave the `sdf_protocol_zigbee_is_enabled()` gate outside.
- [x] 3.4 Audit the twenty call sites for any that read `s_zigbee_alarm_mask` directly rather than going through the setter; those need `atomic_load` or need routing through the setter.

      Checked all 19 call sites of `sdf_app_set_alarm_mask_bits()` and every textual occurrence of `s_zigbee_alarm_mask` in the tree. Nothing reads the variable directly: after this change the only references are the declaration (`sdf_app.c:84`), the `atomic_load` and `atomic_compare_exchange_weak` inside the setter (`:151`, `:162`), and the reset in `sdf_app_init()` (`:1728`, now `atomic_store`). The mask is not exposed in `sdf_app.h`, so no other component can read it either. No call site needed rerouting and none needed an `atomic_load` added.
- [x] 3.5 Host test for concurrent set-of-A and clear-of-B converging to both effects. If the host target cannot drive two tasks into the RMW window reliably, say so and cover the composition logic single-threaded instead — record which was done rather than leaving it ambiguous.

      **Both halves were written** (unblocked by the target runner repair — see the note after 1.16), because on a single-core ESP32-C6 the concurrent version can demonstrate convergence but cannot prove the composition rule:
      - `test_sdf_app_alarm_mask_composition_single_threaded` — **deterministic**. Pins down the rule itself against the real `sdf_app_set_alarm_mask_bits()`: additive set, set+clear of disjoint bits, no-op clear, overlapping set+clear (clear wins), redundant update, clear-all. This is the authoritative half.
      - `test_sdf_app_alarm_mask_concurrent_set_and_clear_converge` — **best-effort stress, not a proof**. Two equal-priority writer tasks each own half the mask (`0x00FF` / `0xFF00`) and hammer it for 1 s, each self-checking after every write that its own half reads back what it wrote and counting losses. Asserts both writers completed >1000 rounds, neither lost a bit, and the mask converges to `0x0000`. It cannot force a preemption inside the compare-exchange window on a single core, so a pass is evidence rather than a guarantee; the test file says so in a comment.

      Both pass on the target under `esp-emu`.

## 4. Repair and extend `doc/rtos_tasks.md`

Runs after group 1, which fixes the numbers this records.

- [x] 4.1 Add the four tasks the table currently omits — `sdf_evt_router`, `led`, `fp_owner`, and the Zigbee attribute task — with their actual priorities, stack sizes and owning components. Read these from the `xTaskCreate` call sites, not from other documentation.
- [x] 4.2 Add the new `sdf_app` task with its priority, stack size, owning component, and the measurement from 0.5-0.6 including the lower-bound caveat.
- [x] 4.3 Recompute the aggregate stack total the document states.

      Table now lists the ten tasks that actually exist, each read from its `xTaskCreate()` call site: `sdf_app` 5/4096, `sdf_evt_router` 5/3072, `sdf_power` 4/4096, `sdf_zigbee` 5/6144, `sdf_zb_attr` 4/3072, `sdf_match` 5/4096, `sdf_enroll` 4/4096, `sdf_admin` 5/4096, `fp_owner` 5/4096, `led` 4/2048. Two figures the document carried were wrong and are corrected in the table *and* in the §2 detail sections that repeated them: `sdf_zigbee` was documented as 8 KB (actually 6144) and `sdf_match` as 6 KB (actually 4096). Total is now 38 KB (38912 B) rather than "~34 KB", with `sdf_ota` excluded because it is not created yet. Also corrected while enumerating: §5.2 still said `sdf_admin` was unregistered with the task watchdog, which `register-admin-task-watchdog` changed.
- [x] 4.4 Update `doc/sdf_sas.md` §6, `doc/software-architecture.md` §6 and `AGENTS.md` where they state a task count or enumerate tasks. The "6 tasks" figures in those documents are the drift being repaired.
- [x] 4.5 Note in `doc/rtos_tasks.md` that `SDF_EVENT_ROUTER_TASK_STACK` is now over-provisioned for the work the dispatch task actually does, with a pointer to the follow-on. Do not change it in this change.

## 5. Close out

- [x] 5.1 Full build for target and host; full host test suite green. Report actual output, including anything that failed.

      Target: `Project build complete.` Only warnings are the two pre-existing `-Wunused-function` ones for `sdf_app_enrollment_result_name` (`sdf_app.c:354`) and `sdf_app_enrollment_state_name` (`:334`), both present before this change and both used only under `SDF_APP_TESTING`.

      Host runner: builds clean (the `ld: warning: ignoring duplicate libraries` line is pre-existing and unrelated). Suite: **310 Tests 0 Failures 11 Ignored**, up from 307 before this change — the four new emit-ban tests minus the one that was rewritten rather than added. Nothing failed.
- [x] 5.2 Boot under `esp-emu` end to end and confirm: subscriber count line reads `21/21`, no emit rejections, no app-queue drops during ordinary boot, no panic.

      `esp-emu` v0.39.0 / esp32c6, 20 s run of the final merged image. `Event router started: 21/21 subscribers registered` present; zero `Rejecting emit from subscriber callback`; zero `App queue full`; zero panics, aborts or watchdog trips. The remaining `E (...)` lines are the three known emulator gaps (`nvs_sec_provider` HMAC eFuse, `gpio_install_isr_service` already installed, fingerprint sensor absent).

      Scope caveat, same as 2.9: emulated time stalls at ~6946 ms once every task blocks, so this covers the boot path only — no application event is dispatched, and therefore no app-queue traffic is exercised. Whether an event reaches the app task is covered by static reading (all nine subscriptions point at the trampoline) rather than by this run.
- [x] 5.3 File the follow-ons: (a) `sdf_ble_companion`'s two subscriber callbacks do cJSON allocation and GATT notify fan-out inside dispatch (`sdf_ble_companion.c:198, 226`) and are not caught by the ban — note that no `ble_npl_eventq` usage exists in the codebase, so deferring to the NimBLE host task is new plumbing; (b) re-measure and reduce `SDF_EVENT_ROUTER_TASK_STACK` now that the deep path has moved; (c) the dispatch-duration watchdog from design.md — Open Questions; (d) the service trampolines have the same critical-priority reordering that D4 fixes for the app queue.

      (a) `sdf_ble_companion_enrollment_complete_handler()` and `..._failed_handler()` (`sdf_ble_companion.c:198`, `:226`) build a cJSON object, print it, and fan a GATT notify out to every authenticated connection — all on the dispatch task. The ban does not catch this: they never call `sdf_event_router_emit()`, so nothing rejects them; they simply hold the dispatch task for the duration. Deferring them is new plumbing, not a repeat of the trampoline pattern: there is no `ble_npl_eventq` usage anywhere in the tree, so handing work to the NimBLE host task has no precedent to copy here.

      (b) Re-measure and reduce `SDF_EVENT_ROUTER_TASK_STACK`. It is 3072 and was sized when subscriber callbacks did real work on it; every subscriber is now a trampoline. Needs its own probe against post-change firmware — noted in `doc/rtos_tasks.md` §2.9 and deliberately not changed here.

      (c) Dispatch-duration watchdog (design.md — Open Questions): the ban is a runtime check on one function and says nothing about how long a callback runs.

      (d) The three service trampolines (`sdf_match_task_event_cb`, `sdf_enroll_task_event_cb`, `sdf_admin_task_event_cb`) all use plain `xQueueSend(state->event_queue, &evt_copy, 0)`, so a `PRIO_CRITICAL` event loses its queue-jumping on the second hop — exactly what D4 fixes for the app queue with `xQueueSendToFront`. `sdf_match` subscribes to `POWER_WAKE`/`POWER_SLEEP` and `sdf_admin` to `SECURITY_LOCKOUT`-adjacent types, so this is reachable today, not theoretical.

      (e) **New, found while instrumenting for group 0:** `sdf_app_on_web_reg_auth_result()` can never persist the first web user. It only saves into a slot where `sdf_storage_web_user_load(i, &existing) == ESP_OK && !existing.valid`, but on fresh NVS every index returns `ESP_ERR_NVS_NOT_FOUND`, so the loop finds no slot and falls through silently — the BLE reply still says authorized. The probe had to pre-seed index 0 with a `valid=false` blob to reach the deep path at all. This is a handler defect independent of this change and is not fixed here.
- [x] 5.4 Record whether the audit round-trip (design.md — D5) showed up in the drop counter from 1.4. If it did, that is the evidence the Open Question asks for; if it did not, say so explicitly so the question closes rather than lingering.

      **It did not show up, and could not have in what was run.** `s_app_evt_dropped` stayed at 0 across every `esp-emu` run, but the counter is not evidence either way: emulated time stalls at ~6946 ms once every task blocks, so no `AUDIT` event was emitted, dispatched, or round-tripped during any run. Zero drops here means zero events, not headroom.

      What can be said without a measurement: the round trip is real in the code path — `sdf_app_emit_audit()` from the app task goes to the router queue, the router dispatches it back through the trampoline into the app queue, and the app task logs it and increments one of the `s_app_audit_err_*` counters. It occupies one of the 10 app-queue slots for that time. The Open Question asks whether that displaces an unlatch under load; answering it needs either hardware or a load harness that can actually drive biometric matches, neither of which exists today. Recording that explicitly so the question is closed as **unanswered for a stated reason** rather than silently dropped.
- [x] 5.5 Run `openspec validate give-sdf-app-a-task --strict`. → `Change 'give-sdf-app-a-task' is valid`.
