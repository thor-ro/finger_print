## Why

`sdf_event_router_emit()` dispatches `PRIO_CRITICAL` events synchronously on the caller's stack, so subscriber callbacks run on whatever task happened to emit. The single production CRITICAL producer — lockout entered, `sdf_services_match.c:226` — therefore drives `sdf_app_on_event()` → `sdf_app_set_alarm_mask_bits()` → `sdf_protocol_zigbee_update_alarm_mask()` on the `sdf_match` task (4096 B stack), where it can block up to 250 ms on the Zigbee state lock plus 1000 ms on `esp_zb_lock_acquire()` and then serialise a ZCL attribute write. A fire-and-forget-looking `emit()` is in fact a 1.25 s worst-case blocking call charged to the fingerprint match path, and it forces callers to contort around it: `sdf_services_match.c:171-201` stages `emit_lockout` into a `bool` and releases `s->lock` before emitting, precisely so a callback cannot run under that lock.

Separately, `sdf_event_router_emit_nonblocking()` (added 2026-08-14 in `8fcc8c9`) is byte-for-byte identical to `sdf_event_router_emit()` apart from the queue send timeout (`0` vs `100 ms`). It exists because the button GPIO callback must not block and the single entry point had no way to express that. This is the same duplication the archived `2026-08-07-cleanup-event-router-api` change removed as `emit_async()`, regrown under a new name six days after the `sdf-event-router` spec declared `emit()` "the router's one and only emit entry point" — the current API is a live violation of that requirement. The axis that actually varies between the two functions is not sync-vs-async, it is *how long the caller may block*, which is what the API should take as a parameter.

## What Changes

- Remove synchronous dispatch from the emit path. `PRIO_CRITICAL` events are enqueued with `xQueueSendToFront()` so they are dispatched ahead of pending non-critical events; all other priorities use `xQueueSendToBack()`. Every subscriber callback then runs on the router task and only on the router task.
- **BREAKING**: `sdf_event_router_emit()` takes an explicit send timeout: `esp_err_t sdf_event_router_emit(const sdf_event_router_event_t *event, uint32_t send_timeout_ms)`. Existing callers pass `100` to preserve today's behaviour.
- **BREAKING**: Remove `sdf_event_router_emit_nonblocking()`. Its two call sites in `sdf_services_button.c` migrate to `sdf_event_router_emit(&evt, 0)`, restoring the single-entry-point property the spec already requires.
- `sdf_event_router_dispatch_sync()` becomes reachable only from the router task loop. Its event-type validation is retained (events reach it from the queue, which does not re-validate).
- Emitting before `sdf_event_router_start()` continues to work for every priority, including CRITICAL: the event is queued and delivered once the dispatch task exists. This is a behaviour change for CRITICAL, which is currently delivered immediately in that window.

Not in scope, deliberately: `SDF_EVENT_ROUTER_SECURITY_LOCKOUT` is emitted at CRITICAL when entered and NORMAL when cleared. Front-of-queue insertion narrows but does not eliminate the resulting ordering inversion — an entered event can still overtake a queued cleared event. Collapsing that type to a single priority is a separate behavioural decision about the security model, not part of unifying the emit API; it is recorded in `design.md` as a known remaining gap.

## Capabilities

### New Capabilities

(none)

### Modified Capabilities

- `sdf-event-router`: the "Emit routes by priority through a single dispatch path" requirement changes — CRITICAL is no longer dispatched on the caller's context but enqueued at the front of the queue; `emit()` gains an explicit send-timeout parameter; the full-queue and pre-start scenarios extend to CRITICAL. The "Dispatch runs without acquiring a lock" requirement's reentrancy scenario changes: a callback emitting a CRITICAL event no longer produces nested dispatch.

## Impact

- `firmware/components/sdf_event_router/include/sdf_event_router.h` — new `emit()` signature; remove `emit_nonblocking()` declaration; update the doc comment describing CRITICAL handling.
- `firmware/components/sdf_event_router/src/sdf_event_router.c` — single emit implementation with `xQueueSendToFront`/`xQueueSendToBack` split; remove `emit_nonblocking()`; make `dispatch_sync()` router-task-only.
- `firmware/components/sdf_services/src/sdf_services_button.c:41,60` — `emit_nonblocking(&evt)` → `emit(&evt, 0)`.
- All other `sdf_event_router_emit()` call sites across `sdf_app`, `sdf_services_*`, `sdf_ble_companion`, `sdf_power`, `sdf_protocol_zigbee` — add the `100` timeout argument. Mechanical; the compiler flags every one.
- `firmware/components/sdf_event_router/test/test_sdf_event_router.c` and `firmware/test_runner/main/test_runner_main.c` — the three `emit_nonblocking` tests fold into `emit()` tests with a `0` timeout; add coverage for CRITICAL being dispatched on the router task rather than inline, and for CRITICAL ordering ahead of queued non-critical events.
- No change to queue depth (`CONFIG_SDF_EVENT_ROUTER_QUEUE_DEPTH=32`), subscriber capacity, task priorities, or the subscribe/start lifecycle.
- `openspec/specs/security-event-emission/spec.md` references `sdf_event_router_emit()` by name without arguments and remains accurate; no delta needed.
