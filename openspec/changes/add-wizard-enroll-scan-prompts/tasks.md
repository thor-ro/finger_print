## 1. Firmware — emit per-scan progress

- [x] 1.1 In `sdf_services_enroll.c`'s `SDF_ENROLL_ACT_EXECUTE_STEP` branch, emit `SDF_EVENT_ROUTER_ENROLLMENT_STEP_COMPLETE` carrying `completed_steps` and the newly entered state, after `xSemaphoreGive(s->lock)`. Evidence: `sdf_enroll_task_emit_step_progress()` (`sdf_services_enroll.c:139`), called from the branch at `sdf_services_enroll.c:315`. The payload reuses `.payload.enrollment = {.step = completed, .status = state}` — the same pairing `sdf_services_emit_enrollment_event()` uses, so a subscriber reads one shape for the start and the advances.
- [x] 1.2 Snapshot the two payload fields under the lock before releasing it, so the emitted numbers are the ones the transition produced. Evidence: `captured`/`next_state` are read at `sdf_services_enroll.c:311-313`, before the `xSemaphoreGive()`; the emit itself happens after it, like every other emit in this file.
- [x] 1.3 Confirm the retry path (`SDF_ENROLL_ACT_RETRY_STEP`) emits nothing — a retried scan captured nothing. Evidence: the branch at `sdf_services_enroll.c:229-236` still only logs, drives the LED and re-arms the retry timer. Asserted by `test_enrollment_retried_scan_announces_nothing`.

## 2. Firmware — notify the companion

- [x] 2.1 Add `sdf_ble_companion_enrollment_step_handler()` building `{"status":"progress","captured":C,"step":N,"total":3}`, modelled on the existing complete/failed handlers. Evidence: `sdf_ble_companion.c:287-320`; `total` comes from `SDF_BLE_COMPANION_ENROLL_SCANS` (`sdf_ble_companion.c:243`), derived from the state enum (`STEP_3 - STEP_1 + 1`) rather than a literal 3, so the two cannot drift apart.
- [x] 2.2 Attach the active enrol request id when there is one, without clearing it (the terminal reply still owes it). Evidence: the `s_um_active_enroll_req_id != 0` guard adds `"req"` but the handler never assigns to that static; only the terminal complete/failed handlers clear it.
- [x] 2.3 Ignore events whose status is not an in-progress scan state (SUCCESS/ERROR/IDLE), so a failed start is not reported as progress. Evidence: the early return on `state < SDF_ENROLLMENT_STATE_STEP_1 || state > SDF_ENROLLMENT_STATE_STEP_3`.
- [x] 2.4 Subscribe to `SDF_EVENT_ROUTER_ENROLLMENT_STEP_COMPLETE` alongside the existing enrolment subscriptions. Evidence: `sdf_ble_companion.c:2097-2101`, next to the COMPLETE/FAILED subscriptions.
- [x] 2.5 Bump `SDF_EVENT_ROUTER_SUBS_BLE_COMPANION` 3 → 4 in `sdf_event_router_capacity.h`. Evidence: `sdf_event_router_capacity.h:14`; the production total goes 23 → 24 (the pool is sized with zero headroom, so an undeclared subscription would fail `sdf_event_router_start()`). AGENTS.md's "undeclared 24th" note updated to 25th.
- [x] 2.6 Confirm the notify path reaches setup-phase connections (first Admin enrolment happens before any account exists). Evidence: it did **not** — `sdf_ble_companion_notify_enroll()` requires `AUTH_STATE_AUTHENTICATED`, so the wizard's unauthenticated setup connection could never receive *any* enrolment notification, progress or terminal. Fixed with `sdf_ble_companion_deliver_enroll_json()` (`sdf_ble_companion.c:260-285`), now used by all three handlers (`:314`, `:342`, `:371`): authenticated connections get it on the Enrollment characteristic, and while `sdf_services_setup_phase_is_armed()` the setup connection gets it on the User-Management characteristic, which is already unauthenticated by design. Keyed on *armed*, not on the setup **state**, because the state advances to `ADMIN_ENROLLED` at the moment the success notification is emitted — gating on state would exclude the wizard exactly when it needs the message.

## 3. Web companion — protocol

- [x] 3.1 Add `isEnrollProgress()` and `enrollProgressOf()` to `src/lib/protocol/usermgmt.ts`, returning captured/step/total with defaults for a device that omits them. Evidence: `usermgmt.ts:58-94`; an absent `total` means `ENROLL_DEFAULT_SCANS`, an absent `captured` is derived from `step - 1`, and both are clamped into range.
- [x] 3.2 Keep the module DOM-free (lint gate) and make sure a progress notification is not classified as a UM reply or a list part. Evidence: `npm run lint` passes (the gate rejects DOM references under `src/lib/protocol/`); `isEnrollProgress()` excludes `isUmReply()` and `isListPart()`, asserted by "is not a user-management reply" / "is not a list part".

## 4. Web companion — state

- [x] 4.1 Drive `wizardEnroll*` from the progress notification: captured count, expected scan, prompt text, percent. Evidence: `session.svelte.ts:80-82` (state), `:860-866` (progress branch of `handleWizardEnrollNotification()`); the percent now derives from captured/total inside `ScanProgress`.
- [x] 4.2 Do the same for the dashboard enrolment panel, which shares the notification. Evidence: `session.svelte.ts:103-105`, `:833-839`.
- [x] 4.3 On success mark every scan captured before advancing; on failure clear the prompt. Evidence: `session.svelte.ts:823-824` and `:847-848` set captured = total, expected = 0; the failure branches (`:830`, `:856`) clear the prompt and leave the reason in the status line.
- [x] 4.4 Keep the no-progress fallback: starting an enrolment shows it in flight and states the scan count. Evidence: `wizardEnrollAdmin()` (`:232-238`) sets captured 0 / expected 1 and the "Place the admin's finger on the sensor - scan 1 of 3." prompt before any notification arrives; the dashboard's `enroll()` (`:769-787`) starts at expected 0 and only moves to 1 once the admin authorizes, because the authorizing Admin scan is not one of the three.
- [x] 4.5 Share one prompt writer between both surfaces. Evidence: `scanPrompt()` (`session.svelte.ts:36`), parameterised by whose finger it is, so the wizard and the dashboard cannot word the same state differently.

## 5. Web companion — UI

- [x] 5.1 `ScanProgress.svelte`: one marker per scan (captured / expected / outstanding), tokens only, no colour literals. Evidence: rewritten around `{ captured, expected, total, message }`; `npm run themes` and `npm run lint` pass (both reject colour literals under `src/`).
- [x] 5.2 `WizardView.svelte`: ask for the current scan by number; keep the button as the starting action only. Evidence: the step-1 copy now states `{session.wizardEnrollTotal}` scans asked "one at a time"; the button is disabled while an enrolment is in flight and reads "Enrolment in progress…".
- [x] 5.3 Markers carry an accessible label so the count is not conveyed by colour alone. Evidence: `labelOf()` renders "Scan N of T: captured / waiting for your finger / not yet taken" in a `.visually-hidden` span per marker; the prompt sits in a `role="status"` region. Asserted by "states each marker in text, so the count is not carried by colour alone".

## 6. Tests

- [x] 6.1 `sdf_services`: a successful scan advance emits a step event with the captured count and next state. Evidence: `test_enrollment_captured_scan_announces_the_next_one` — scripts an `ENROLL_1` success on the Linux mock UART and asserts one event with `step == 1`, `status == SDF_ENROLLMENT_STATE_STEP_2`.
- [x] 6.2 `sdf_services`: a retried scan emits none. Evidence: `test_enrollment_retried_scan_announces_nothing` — a `SDF_FINGERPRINT_ACK_FAIL` reply leaves zero events, the state at `STEP_1` and `completed_steps` at 0.
- [x] 6.3 Vitest: `isEnrollProgress()` / `enrollProgressOf()`, including the defaults and the not-a-reply classification. Evidence: 8 tests in `usermgmt.test.ts` › "enrolment progress notifications", ending with a real device frame decoded end to end.
- [x] 6.4 Vitest: the session handler advances the prompt on progress, completes on success, clears on failure. Evidence: 7 tests in `session.test.ts` › "wizard asks for one fingerprint scan at a time" plus 2 in "dashboard enrolment shares the per-scan prompting".
- [x] 6.5 Vitest: `ScanProgress` renders the right marker states. Evidence: `ScanProgress.test.ts`, 7 tests, including a device-reported total of 4 to prove the three is not baked in.
- [x] 6.6 Register the two firmware tests in `firmware/test_runner/main/test_runner_main.c`. Evidence: externs at `:189-190` and `RUN_TEST` calls at `:695-696`, inside the `#ifdef CONFIG_IDF_TARGET_LINUX` block — a scan can only succeed against the Linux mock UART's scripted-response hook.

## 7. Verification

- [x] 7.1 `npm run gate` in `web-companion/` (svelte-check, lint, themes, vitest, bundle budget). Evidence: green — svelte-check 377 files / 0 errors, lint and theme checks clean, **10 test files / 100 tests passed**, budget 47.1 KB initial against a 48.1 KB cap and 54.2 KB total against 55.3 KB.
- [x] 7.2 Host Unity run (`firmware/test_runner`, linux target) green. Evidence: **428 Tests, 0 Failures, 12 Ignored** (426/0/12 before), both new tests `PASS` by name. The run also picked up a stale generated `firmware/test_runner/sdkconfig` still carrying `CONFIG_SDF_FP_BAUD_RATE=115200` from before the baud fix; realigned to 19200 so the host log no longer states a baud the sensor does not answer at.
- [x] 7.3 Chip-target run green (esp-emu or hardware). Evidence: **352 Tests, 0 Failures, 13 Ignored** under esp-emu (esp32c6, out-of-tree `/private/tmp/tr_hw`) — unchanged from the baseline, as expected: both new tests are host-only.
- [ ] 7.4 On hardware: run the wizard against the device and confirm each scan updates the visualization and prompts for the next.

## 8. Docs

- [x] 8.1 `doc/user_manual.md` — enrolment flow now prompts per scan. Evidence: setup step 3 states the wizard asks one scan at a time and marks each one the device confirms; the dashboard capture step notes the panel marks the scan and asks for the next.
- [x] 8.2 `doc/sdf_sas.md` — new event consumer / notification. Evidence: the enrolment sequence gains the two `ENROLLMENT_STEP_COMPLETE` emissions, the executor description gains the per-scan emit step, and the Enrollment-characteristic section documents the `{"status":"progress","captured":C,"step":N,"total":T}` shape, the carries-but-does-not-consume-the-id rule, and the setup-phase delivery path.
