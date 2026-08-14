## Why

Button press handling runs entirely inside `iot_button`'s internal `esp_timer` scan callback. `button_cb()` invokes the registered callbacks (`sdf_button_cb` / `sdf_button_single_click_cb`) directly, which call `sdf_button_dispatch_action()` — so everything that follows a button press executes **on the shared `esp_timer` task**, at priority 22, ahead of nearly every other task in the system. That task dispatches all `esp_timer` callbacks in the firmware, including `iot_button`'s own scan timer; blocking it blocks all of them.

What currently runs there:

| Path | Work done on the `esp_timer` task |
|---|---|
| Any press | `xSemaphoreTake(s->lock, 250ms)` — bounded, but a 250 ms priority-22 stall is already bad |
| Single-click | `sdf_services_get_setup_state()` → **`sdf_storage_nuki_load()`, an NVS/flash read** (`sdf_services.c:1214`) |
| Double-click, zero users | `sdf_ble_companion_open_pairing_window()` → NimBLE advertising restart |
| 8-second hold, zero users | `sdf_storage_erase_all()`, then **`fp_delete_all_users()` — a multi-second UART round trip to the fingerprint sensor** — then Zigbee factory reset, then `esp_restart()`, which never returns |

The last row is the severe one: a factory reset on an unclaimed device parks the system's highest-priority timer task in a multi-second blocking UART transaction and then reboots out of it. The single-click flash read is the most frequently hit one.

None of this is what the design intended. `SDF_EVENT_ROUTER_BUTTON_PRESS` exists in the event enum with **zero subscribers**; `sdf_button_task_emit_button_press()` and `sdf_button_task_emit_admin_action_request()` are written and never called; and `sdf_admin_task` already subscribes to `SDF_EVENT_ROUTER_ADMIN_ACTION_REQUEST` and already implements the pending-action handler (`sdf_services_admin.c:52`, `:119-156`). The producer/consumer split was built and then bypassed. This change connects it.

## What Changes

- **Button callbacks become pure producers.** `sdf_button_cb` / `sdf_button_single_click_cb` emit a `SDF_EVENT_ROUTER_BUTTON_PRESS` event carrying the *gesture* and return. They perform no lock acquisition, no flash access, and no action execution.
- **Gesture-to-action resolution moves off the timer task.** The single-click gesture's state-dependent resolution (which reads enrolled-user count and probes persisted Nuki credentials) moves to the consumer, because that resolution is itself a flash read. The button emits "single click", not "ENROLL" or "NUKI_PAIR".
- **`sdf_admin_task` becomes the consumer.** It subscribes to `SDF_EVENT_ROUTER_BUTTON_PRESS`, resolves the gesture to an admin action, and then runs the existing authorization flow it already implements — the bootstrap-bypass helper first, then the pending-action gate.
- **Backpressure drops the press rather than blocking the producer.** `sdf_event_router_emit()` currently uses a 100 ms `xQueueSend` timeout for non-critical events (`sdf_event_router.c:203`). The button producer uses a non-blocking send: if the router queue is full, the press is dropped and logged. A dropped press is strictly better than a 100 ms priority-22 stall, and the user's recourse — press again — is immediate and obvious.
- Define an explicit press-type enumeration for the currently untyped `uint8_t press_type` field of `sdf_event_router_button_payload_t`, covering the three bound gestures (single-click, double-click, long-press).
- `sdf_button_dispatch_action()`'s remaining body (the pending-action gate) moves to the consumer side. The symbol is retained with its current signature and non-static linkage, since the host tests drive it directly.

## Capabilities

### Modified Capabilities
- `sdf-services-tasks`: button press handling becomes an event-router producer/consumer split — press detection stays in the driver callback while resolution, authorization, and execution move to a normal-priority task; adds requirements bounding the work permitted in a press-detection callback and defining drop-on-backpressure behavior; and updates the two button requirements whose observable timing or resolution site changes.

## Impact

- Code: `firmware/components/sdf_services/src/sdf_services_button.c` (callbacks reduced to emit-and-return; resolution and dispatch bodies moved out), `sdf_services_admin.c` (new `BUTTON_PRESS` subscription, gesture resolution, dispatch), `sdf_event_router.h` (press-type enum), `sdf_services_internal.h`.
- **Observable timing change**: a button press's LED feedback and action execution now occur after an event-router queue hop instead of synchronously in the press callback. Under normal load this is sub-millisecond; under load it is bounded by `sdf_admin_task`'s scheduling. This is a real behavioral difference and is the main thing to validate on hardware.
- **New failure mode**: a press can be dropped if the event-router queue is saturated. Previously impossible, since dispatch was synchronous.
- No public API signature change.
- **Sequencing**: lands **third** of four. Approved order: `unify-pending-admin-action-led-mapping` → `centralize-unclaimed-device-bootstrap` → `dispatch-admin-actions-off-esp-timer` → `quiesce-poll-loops-light-sleep`. It depends on the first (otherwise routing `BLE_PAIRING_WINDOW` through `sdf_admin_task` silently loses its cyan pulse, since that task's LED switch has no case for it) and the second (otherwise the zero-users bootstrap branch has no home on the consumer side).
- **Spec-delta overlap**: `quiesce-poll-loops-light-sleep` also MODIFIES "State-Dependent Single-Click Setup Action" and "Double-Press Requests BLE Companion Pairing Window", solely to strip `sdf_button_task` naming. Since this change lands first and reworks both more thoroughly, that change's deltas for them are expected to be dropped at its archive time rather than reconciled. The two changes are otherwise independent — `quiesce-poll-loops-light-sleep` removes the button *task*, this one moves the button *dispatch*.
- **Hands off to** `quiesce-poll-loops-light-sleep` in two places: it makes `sdf_button_task` unambiguously vestigial (dispatch no longer passes through it at all), and it makes the button-press emit helper — dead code today — live, which that change's task list has been amended to account for.
- Out of scope: `iot_button`'s own scan-timer behavior and its `enable_power_save` setting (handled by `quiesce-poll-loops-light-sleep`), the event router's own 100 ms send timeout for other producers, and the contents of `sdf_app_on_admin_action` — including whether a multi-second factory-reset sequence should itself be broken up now that it runs on a normal-priority task.
