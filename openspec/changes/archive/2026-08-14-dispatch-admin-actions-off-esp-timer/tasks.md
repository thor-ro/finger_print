## 0. Prerequisites

- [x] 0.1 Confirm `unify-pending-admin-action-led-mapping` has landed — `sdf_admin_task`'s pending-action LED path must already cover `BLE_PAIRING_WINDOW`, or the double-click loses its cyan pulse when it moves.
- [x] 0.2 Confirm `centralize-unclaimed-device-bootstrap` has landed — the zero-enrolled-users bypass must already live in a shared, origin-parameterized helper callable from the consumer side.

## 1. Type the button press event

- [x] 1.1 Define an enumeration for `sdf_event_router_button_payload_t.press_type` in `sdf_event_router.h`, covering single-click, double-click, and long-press.
- [x] 1.2 Populate `press_duration_ms` for the long-press gesture (design Open Question — default is to populate it); leave it zero for the click gestures.

## 2. Add a non-blocking emit path

- [x] 2.1 Add a non-blocking emit variant to `sdf_event_router` that uses a zero `xQueueSend` timeout instead of the current 100 ms (`sdf_event_router.c:203`), returning a distinguishable result when the queue is full.
- [x] 2.2 Log dropped events at warning level from the router, so drops are countable in one place rather than per-producer.
- [x] 2.3 Confirm existing producers are untouched — they keep the current 100 ms-timeout emit.

## 3. Reduce the button callbacks to producers

- [x] 3.1 `sdf_services_button.c`: change `sdf_button_cb` and `sdf_button_single_click_cb` to emit `SDF_EVENT_ROUTER_BUTTON_PRESS` at `SDF_EVENT_ROUTER_PRIO_HIGH` with the corresponding `press_type`, via the non-blocking emit, and return.
- [x] 3.2 Verify the callbacks now take no lock, perform no storage access, and call no `led_*` or `admin_action_cb` — this is the requirement being satisfied, so check it explicitly against the compiled call graph, not just by reading the function bodies.
- [x] 3.3 Move `sdf_button_resolve_single_click_action()` to the consumer side, or expose it so the consumer can call it. It must no longer be reachable from a timer callback, since it performs a flash read via `sdf_services_get_setup_state()`.
- [x] 3.4 Delete the now-superseded `sdf_button_task_emit_button_press()` / `sdf_button_task_emit_admin_action_request()` helpers if `quiesce-poll-loops-light-sleep` has not already removed them; otherwise confirm they are gone.

## 4. Make `sdf_admin_task` the consumer

- [x] 4.1 Subscribe `sdf_admin_task` to `SDF_EVENT_ROUTER_BUTTON_PRESS` alongside its existing `ADMIN_ACTION_REQUEST` subscription, matching the existing subscription's `min_prio` semantics.
- [x] 4.2 Add a `SDF_EVENT_ROUTER_BUTTON_PRESS` case to its event switch that maps `press_type` to an admin action: single-click via the state-dependent resolution, double-click to `BLE_PAIRING_WINDOW`, long-press to `FACTORY_RESET`.
- [x] 4.3 Route the resolved action through the bootstrap-bypass helper (local-physical origin), then — if not bypassed — through `sdf_button_dispatch_action()`'s retained pending-action body.
- [x] 4.4 Collapse the duplication: have the existing `ADMIN_ACTION_REQUEST` case call the same retained pending-action body instead of its own inline copy.
- [x] 4.5 Confirm the consumer's lock discipline — the pending gate holds `s->lock` across only the LED call (non-blocking), and the bypass helper releases the lock before executing, per `centralize-unclaimed-device-bootstrap` design Decision 3.
- [x] 4.6 Verify `sdf_admin_task`'s event queue depth is adequate for burst presses, and that its `xQueueReceive` wake is prompt enough that press-to-LED latency is dominated by scheduling, not by its 100 ms timeout.

## 5. Verification

- [x] 5.1 Run the host test suite (`sdf_services`) — the existing tests drive `sdf_button_dispatch_action()` directly and must still pass unchanged, since its signature and body are retained.
- [x] 5.2 Add host tests: a `BUTTON_PRESS` event for each gesture resolves to the expected admin action; a double-click consumed while another action is pending does not displace it.
- [x] 5.3 Add a host test that a dropped press (emit fails) leaves no pending action, produces no LED indication, and executes nothing.
- [x] 5.4 Hardware/emulator, **claimed device**: single-click, double-click, and 8-second hold each still produce the correct LED indication and the correct action.
- [x] 5.5 Hardware/emulator, **unclaimed device (zero users)**: single-click starts enrollment; double-click opens the BLE pairing window; 8-second hold runs the full factory reset to reboot. Confirm the factory reset no longer executes on the `esp_timer` task.
- [x] 5.6 Measure press-to-LED latency for all three gestures and compare against the pre-change synchronous behavior; confirm no user-perceptible regression.
- [x] 5.7 Confirm `iot_button`'s own scan timer is no longer starved during a factory reset — the specific `esp_timer`-task-blocking symptom this change exists to remove.
