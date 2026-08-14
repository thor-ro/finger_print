## Context

```
                            BEFORE
  ┌────────────────────────────────────────────────────────────┐
  │  esp_timer task  (priority 22, shared by ALL esp_timers)   │
  │                                                            │
  │  iot_button scan timer ──▶ button_cb()                     │
  │        └─▶ sdf_button_cb / sdf_button_single_click_cb      │
  │              └─▶ sdf_button_resolve_single_click_action()  │
  │                    └─▶ sdf_storage_nuki_load()   ◀── FLASH │
  │              └─▶ sdf_button_dispatch_action()              │
  │                    ├─ xSemaphoreTake(lock, 250ms)          │
  │                    └─ [users==0] admin_action_cb()         │
  │                         └─▶ sdf_app_on_admin_action()      │
  │                              ├─ open_pairing_window() NimBLE│
  │                              └─ FACTORY_RESET:             │
  │                                   erase_all()              │
  │                                   fp_delete_all_users() UART│  ◀── seconds
  │                                   zigbee_factory_reset()   │
  │                                   esp_restart()            │  ◀── never returns
  └────────────────────────────────────────────────────────────┘

                            AFTER
  ┌──────────────────────────────┐      ┌────────────────────────────┐
  │ esp_timer task (prio 22)     │      │ sdf_admin_task (normal)    │
  │                              │      │                            │
  │ button_cb()                  │      │ xQueueReceive(...)         │
  │   └─▶ emit BUTTON_PRESS ─────┼─────▶│   └─ resolve gesture       │
  │        (non-blocking send)   │      │        └─ nuki_load() FLASH│
  │       return                 │      │      ─ bootstrap helper    │
  │                              │      │      ─ pending gate + LED  │
  │  bounded, no lock, no I/O    │      │      ─ admin_action_cb()   │
  └──────────────────────────────┘      └────────────────────────────┘
```

The consumer side is not new construction. `sdf_admin_task` already subscribes to `SDF_EVENT_ROUTER_ADMIN_ACTION_REQUEST` and already implements the pending-action gate (`sdf_services_admin.c:52`, `:119-156`). `SDF_EVENT_ROUTER_BUTTON_PRESS` already exists in the event enum with a `{press_type, press_duration_ms}` payload and zero subscribers. `sdf_button_task_emit_button_press()` already exists and is never called. This change wires up parts that were built and then bypassed.

## Goals / Non-Goals

**Goals**
- Nothing that can block for longer than a few microseconds runs on the `esp_timer` task as a result of a button press.
- Gesture → action resolution happens where it can afford a flash read.
- The consumer is the existing `sdf_admin_task`, reusing its existing handler rather than adding a task or a queue.
- The press producer never blocks, even under event-router saturation.

**Non-Goals**
- Changing which gesture maps to which action, or the authorization rules.
- Making `sdf_app_on_admin_action`'s factory-reset sequence itself incremental. Once it is off the `esp_timer` task, a multi-second blocking sequence on a normal-priority task that ends in `esp_restart()` is acceptable. Revisit separately if not.
- Touching `iot_button`'s scan cadence or `enable_power_save` — that is `quiesce-poll-loops-light-sleep`.

## Decisions

### Decision 1: Emit the gesture, not the resolved action
The obvious shape is "resolve in the callback, emit `ADMIN_ACTION_REQUEST`" — it reuses `sdf_admin_task`'s existing subscription with no new event plumbing. Reject it: resolution *is* the flash read. `sdf_button_resolve_single_click_action()` calls `sdf_services_get_setup_state()`, which calls `sdf_storage_nuki_load()` (`sdf_services.c:1214`). Emitting a resolved action leaves the single-click path — the most frequently exercised gesture — still doing NVS work at priority 22.

So the producer emits `BUTTON_PRESS` with the gesture, and the consumer resolves. This also matches the event type that already exists for exactly this purpose.

**Consequence:** resolution now happens at consumption time, so a setup-state change between detection and consumption changes the resolved action. This is specified explicitly rather than left implicit. In practice the window is sub-millisecond and setup state changes require a completed enrollment or pairing, so a real collision is not plausible — but the spec says which side of the race wins.

### Decision 2: Type the `press_type` field
`sdf_event_router_button_payload_t.press_type` is a bare `uint8_t` with no defined values (`sdf_event_router.h:129`) — a consequence of having had no consumer. Define an enumeration for the three bound gestures. Leave `press_duration_ms` as-is; it is unused by the resolution logic but is meaningful payload for any future consumer and costs nothing.

### Decision 3: Non-blocking send, drop on backpressure
`sdf_event_router_emit()` uses `xQueueSend(..., pdMS_TO_TICKS(100))` for non-critical events (`sdf_event_router.c:203`). Inheriting that would replace a multi-second worst case with a 100 ms worst case on the `esp_timer` task — better, but still a priority-22 stall, and still a bug of the same kind.

The button producer therefore sends with a zero timeout. If the queue is full, log and drop. Rationale: a button press is a *fresh user intent* with a trivially available retry — the user presses again. Blocking the system's highest-priority timer task to preserve a press that the user can reissue in under a second is the wrong trade.

This requires either a non-blocking emit variant on the event router or a direct queue send from the producer. Prefer adding an explicit non-blocking emit to the router over bypassing it, so the router stays the single delivery path and the drop is countable in one place.

**Consequence:** a new failure mode that did not exist when dispatch was synchronous. Specified explicitly, including that a dropped press must leave no partial state.

### Decision 4: `sdf_admin_task` is the consumer, not a new task
It already owns the pending-action gate, already has an event queue and subscription machinery, and already runs at normal priority. Adding a second subscription to an existing task is strictly cheaper than a new task, and it puts gesture handling next to the authorization logic it feeds.

**Trade-off:** button responsiveness is now coupled to `sdf_admin_task`'s scheduling. That task's loop currently blocks on `xQueueReceive` with a 100 ms timeout, so an event wakes it immediately — responsiveness is bounded by scheduling latency, not by the poll interval. Acceptable.

### Decision 5: Retain `sdf_button_dispatch_action()` as the consumer-side body
Its remaining body after `centralize-unclaimed-device-bootstrap` lands is the pending-action gate plus the LED call. That body is still exactly what the consumer needs to run. Keep the function, keep its signature and non-static linkage — the host tests in `test_sdf_services.c` drive it directly and are the main regression gate for this change. It simply gains a new caller (the consumer) and loses its old one (the timer callback).

This also means `sdf_admin_task`'s existing inline copy of the pending-action gate should call it too, collapsing the last of that duplication.

### Decision 6: Ordering relative to the other two changes
Land `unify-pending-admin-action-led-mapping` and `centralize-unclaimed-device-bootstrap` first, in that order. Without the first, routing `BLE_PAIRING_WINDOW` through `sdf_admin_task` silently drops its cyan pulse. Without the second, the zero-users bootstrap branch has to be hand-carried across the task boundary as part of this change, mixing a security-sensitive relocation into a concurrency fix.

## Risks / Trade-offs

- **[Risk]** Perceived button latency regresses if `sdf_admin_task` is starved. → **Mitigation:** it runs at normal priority with an event-driven wake; measure press-to-LED latency on hardware for all three gestures and compare against the current synchronous behavior.
- **[Risk]** The zero-users double-click path now opens the BLE pairing window from `sdf_admin_task` rather than the timer task, changing which task NimBLE calls originate from. → **Mitigation:** `sdf_ble_companion_open_pairing_window()` is already called from other non-timer contexts via the BLE-originated admin path; verify on hardware with an unclaimed device.
- **[Risk]** A press dropped under backpressure is invisible to the user beyond "nothing happened". → **Mitigation:** log at warning level and count it; the event-router queue would have to be saturated by other traffic for this to occur, which is itself worth alerting on.
- **[Trade-off]** Synchronous dispatch was easy to reason about and impossible to drop. The producer/consumer split trades that for bounded timer-task work. Given the worst case being traded away is a multi-second UART transaction at priority 22, the trade is clearly worth it — but it is a real loss of simplicity.

## Migration Plan

No persisted state, no protocol surface, no staged rollout. Land after its two prerequisites. Validate on hardware before merging: all three gestures on both a claimed and an unclaimed device, with press-to-LED latency measured.

## Open Questions

- Should `press_duration_ms` be populated meaningfully for the long-press gesture, or left zero? Nothing consumes it; populating it is nearly free and makes the event self-describing. Defaulting to populating it.
