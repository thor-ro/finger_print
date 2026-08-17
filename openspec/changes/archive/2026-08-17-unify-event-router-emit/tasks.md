## 1. Router API and implementation

- [x] 1.1 In `firmware/components/sdf_event_router/include/sdf_event_router.h`, change `sdf_event_router_emit()` to `esp_err_t sdf_event_router_emit(const sdf_event_router_event_t *event, uint32_t send_timeout_ms)` and rewrite its doc comment: every priority is enqueued for the router task, `PRIO_CRITICAL` goes to the front of the queue, callbacks never run on the caller's context, and `send_timeout_ms == 0` never blocks.
- [x] 1.2 Remove the `sdf_event_router_emit_nonblocking()` declaration from the header.
- [x] 1.3 In `firmware/components/sdf_event_router/src/sdf_event_router.c`, collapse `emit()` and `emit_nonblocking()` into the single new `emit()`: keep the existing NULL / `initialized` / `INTERNAL_WAKE` / out-of-range validation, then `xQueueSendToFront()` for `PRIO_CRITICAL` and `xQueueSendToBack()` for all other priorities, both with `pdMS_TO_TICKS(send_timeout_ms)`, returning `ESP_ERR_NO_MEM` and logging on failure.
- [x] 1.4 Delete the `sdf_event_router_emit_nonblocking()` definition.
- [x] 1.5 Rename `sdf_event_router_dispatch_sync()` to `sdf_event_router_dispatch()`, keeping it `static` and keeping its event-type validation (the queue does not re-validate before `head_by_type[]` is indexed). Confirm the router task loop is its only remaining caller.

## 2. Call-site migration

- [x] 2.1 Migrate the two non-blocking sites to a zero timeout: `sdf_services_button.c:41` and `:60` become `sdf_event_router_emit(&evt, 0)`.
- [x] 2.2 Confirm the `iot_button` callbacks at those two sites are dispatched from a task context and not an ISR — `xQueueSendToFront()` is not ISR-safe. If either can run in ISR context, stop and raise it before proceeding; it would be a pre-existing bug (`xQueueSend` is equally unsafe there) requiring its own fix.
- [x] 2.3 Add the `100` timeout argument to the 16 remaining production call sites, preserving today's behaviour: `sdf_protocol_ble.c:399`; `sdf_app.c:1260`; `sdf_services.c:213,472`; `sdf_services_match.c:108,216,230,259`; `sdf_services_admin.c:124`; `sdf_services_enroll.c:130,142`; `sdf_protocol_zigbee.c:391`; `sdf_power.c:118,137,160,716`.
- [x] 2.4 Build for the ESP target and confirm zero remaining compile errors, so no call site was missed; grep the tree to confirm `emit_nonblocking` has no remaining references outside tests.
- [x] 2.5 Review `sdf_services_match.c:171-201` — the `emit_lockout` boolean staging that defers the emit until after `xSemaphoreGive(s->lock)`. Leave the deferral in place (emitting under a lock is still worth avoiding) but update its surrounding comment if it justifies itself by reference to synchronous dispatch.

## 3. Tests

- [x] 3.1 In `firmware/components/sdf_event_router/test/test_sdf_event_router.c`, add the timeout argument to all existing `sdf_event_router_emit()` calls.
- [x] 3.2 Fold the three `emit_nonblocking` tests into `emit()` tests passing a `0` timeout: `test_sdf_event_router_emit_nonblocking_delivers`, `_null_args`, and `_rejects_internal_wake_and_invalid_type`. Rename them accordingly and update the `extern` declarations and `RUN_TEST` entries in `firmware/test_runner/main/test_runner_main.c:194-197,538-541`.
- [x] 3.3 Add a test that a `PRIO_CRITICAL` emit does not invoke its subscriber before `emit()` returns, covering the "Critical event is dispatched on the router task" scenario.
- [x] 3.4 Add a test that a `PRIO_CRITICAL` event queued behind pending non-critical events is dispatched first, covering the "Critical event is dispatched ahead of queued non-critical events" scenario.
- [x] 3.5 Add a test that two non-critical events are dispatched in emission order, covering the "Non-critical events are dispatched in arrival order" scenario.
- [x] 3.6 Add a test that a `PRIO_CRITICAL` emit from inside a subscriber callback is enqueued rather than dispatched inline — the callback returns before the second dispatch begins — covering the revised "Reentrant critical emit from a callback is safe" scenario.
- [x] 3.7 Add a test that a `PRIO_CRITICAL` event emitted after `init()` but before `start()` is delivered once `start()` runs, covering the extended "Emit before start is delivered after start" scenario.
- [x] 3.8 Add a test that a full queue causes `PRIO_CRITICAL` emit to return `ESP_ERR_NO_MEM`, covering the extended "Full queue drops the event" scenario.
- [x] 3.9 Run the host test suite via `rtk` and confirm all event router tests pass.

## 4. Documentation and verification

- [x] 4.1 Fix `doc/sdf_sas.md:380`, which still shows `emit_async(BIOMETRIC_MATCH)` in a sequence diagram for a function removed in the archived `2026-08-07-cleanup-event-router-api` change.
- [x] 4.2 Update `todo` items 3 and 8 to reflect what this change resolves and what it deliberately leaves open (the `SECURITY_LOCKOUT` two-priority ordering inversion, per `design.md` — Risks).
- [x] 4.3 Build and boot under `esp-emu`; confirm the router logs `Event router started: N/M subscribers registered` and that boot completes without panic. Treat any panic as a blocker rather than an emulator fidelity artifact.
- [x] 4.4 Under the emulator, drive the lockout path past the failed-attempt threshold and confirm the `SECURITY_LOCKOUT` event still reaches `sdf_app_on_event()` and the Zigbee alarm mask is updated — now from the router task rather than `sdf_match`. **Partially substituted**: the failed-attempt threshold is not reachable under `esp-emu` (no fingerprint sensor, and enrollment needs a GPIO button press the emulator cannot inject), so the event itself was injected instead — a `PRIO_CRITICAL` `SECURITY_LOCKOUT` emitted from the `main` task via temporary, since-reverted instrumentation. It was handled on task `sdf_evt_router`, set alarm mask `0x0004` (`BIOMETRIC_LOCKOUT`) on that same task, and produced `AUDIT BIOMETRIC_LOCKOUT user=0 status=5 detail=5`. `emit()` returned `ESP_OK` after dispatch had already completed, confirming the router task preempts the emitter rather than the emitter running the callback.
- [x] 4.5 Run `openspec validate unify-event-router-emit --strict` and confirm the change is still valid after any spec edits made during implementation.
