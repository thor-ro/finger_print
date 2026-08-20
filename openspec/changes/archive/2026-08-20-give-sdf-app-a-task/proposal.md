## Why

`sdf_app` owns nine event-router subscriptions and no task of its own. Every one of its handlers therefore executes on the router's dispatch task, `sdf_evt_router` — a 3072-byte stack shared by all twenty-one subscribers and the only consumer of the router queue. Three consequences follow from that single fact:

- **The router can block on the queue only it drains.** `sdf_app_on_event()` calls `sdf_app_emit_audit()` in six branches (`sdf_app.c:802, 810, 818, 823, 828, 858`), which emits `SDF_EVENT_ROUTER_AUDIT` with the prevailing 100 ms timeout. When the queue is full at that moment the dispatch task waits for space that only it can create — a guaranteed dead wait, 100 ms of stalled dispatch followed by a certain drop, at exactly the moment the system is busiest.
- **Unbudgeted work runs inside dispatch.** `sdf_app_on_web_reg_auth_result()` (`sdf_app.c:896`) runs a 10,000-iteration PBKDF2-HMAC-SHA256, up to five NVS loads plus a save, and a `portMAX_DELAY` semaphore take, all before the dispatch task can look at the next event. Nothing else — button presses, lock actions, power events, enrollment steps — is serviced meanwhile. This is the larger defect; the emit stall is its most visible symptom.
- **The dispatch task's stack has never been measured** against that path.

Tracing it surfaced one adjacent defect in the same file worth fixing together: `s_zigbee_alarm_mask` (`sdf_app.c:114`) is read-modify-written from twenty sites across at least two tasks — the router task via `sdf_app_on_event()`, the NimBLE host task via `sdf_app_on_message()` — with no synchronization. Because the downstream Zigbee attribute cache coalesces to the latest recorded value, a lost update leaves a wrong alarm mask latched rather than self-correcting on the next write: a raised alarm silently dropped, or a cleared alarm left latched.

The ten service-task subscriptions already solve the general problem: their callbacks copy the event to a queue their own task owns and return. `sdf_app` is the component that does not.

## What Changes

- **Give `sdf_app` a task.** `sdf_app_on_event()`, `sdf_app_on_web_reg_auth_result()` and `sdf_app_on_admin_action_complete()` are replaced as subscriber callbacks by a single trampoline that copies the event to a queue owned by a new `sdf_app` task; the existing handler bodies run on that task. No subscription is added or removed, so router capacity is unchanged. PBKDF2, the NVS write, the `portMAX_DELAY` take and the six audit emits all leave the dispatch task in one step.
- **Ban re-entrant emit.** `sdf_event_router_emit()` rejects calls made from within dispatch with `ESP_ERR_INVALID_STATE`, using a dispatch-scoped flag owned by the router task. After the trampoline lands there is no violator left, so this is purely preventive — a guard against the next subscriber that reaches for an inline emit. **BREAKING** for any future caller that relies on emitting from a subscriber callback.
- **Synchronize `s_zigbee_alarm_mask`.** The read-modify-write becomes atomic against concurrent callers. `sdf_app_set_alarm_mask_bits()` stays non-blocking and safe to call from a subscriber callback, a BLE host callback, and the lock-flow callbacks. Moving the router-side call sites onto the app task does not remove this race — `sdf_app_on_message()` still runs on the NimBLE host task.
- **Size the new task's stack by measurement.** Probe the deepest handler path under `esp-emu` and set `SDF_APP_TASK_STACK` from the result. This runs **first**, against current `main`, because guessing the stack and discovering the answer on hardware is the failure mode this task exists to avoid.
- **Bring `doc/rtos_tasks.md` back in sync.** The `task-architecture` capability makes that file the single source of truth for task definitions, priorities, and stacks. It lists six tasks; the firmware creates nine (`sdf_evt_router`, `led` and `fp_owner` are absent). This change adds a tenth, so the drift is repaired in the same pass rather than extended.

Explicitly **not** in scope:

- Splitting the slow web-registration handler onto a second task. One task is proposed; see `design.md` — D1 for why, and for what would justify revisiting.
- `sdf_ble_companion`'s two subscriber callbacks (`sdf_ble_companion.c:198, 226`), which do cJSON allocation and GATT notify fan-out inside dispatch. They are not caught by the ban and are not fixed here. There is no `ble_npl_eventq` usage anywhere in the codebase, so deferring them to the NimBLE host task is new plumbing rather than reuse of an existing mechanism — tracked as a follow-on.
- A dispatch-duration watchdog. Sketched in `design.md` — Open Questions.

## Capabilities

### New Capabilities

- `sdf-app-task`: The application component's task — its trampoline contract, event-queue behaviour under backpressure, priority handling, watchdog participation, and the ordering constraint between subscription registration and task creation. This mirrors what `sdf-services-tasks` already specifies for the match, enroll and admin tasks, for the one component that has subscriptions but no task.

### Modified Capabilities

- `sdf-event-router`: The "Dispatch runs without acquiring a lock" requirement contains a scenario — *"Reentrant critical emit from a callback is safe"* — that blesses precisely the behaviour being banned, and asserts the re-emitted event is dispatched "without being dropped", which is already untrue when the queue is full. That scenario is replaced by one requiring rejection.
- `zigbee-attribute-reporting`: Adds a requirement covering the caller-side alarm-mask shadow. The existing requirements govern the Zigbee component's own state; the mask composed in `sdf_app` before `sdf_protocol_zigbee_update_alarm_mask()` is called sits outside them, which is why the race is invisible to the current spec.
- `task-architecture`: The "Documentation updates" requirement's scenarios are written against a six-task snapshot that no longer holds. Generalized so the documented table is required to match whatever set of tasks the firmware creates, and a requirement is added making that correspondence checkable rather than a point-in-time assertion.

### Unchanged Capabilities

- `security-event-emission` requires audit events to travel through `sdf_event_router_emit()` as `SDF_EVENT_ROUTER_AUDIT`, with `sdf_app_on_event()` subscribing at NORMAL priority. **This change needs no delta against it.** The event type, the subscription, and `sdf_app_emit_audit()`'s body all survive intact; only the context the subscriber runs on changes. An earlier draft of this change collapsed audit to a direct call and had to reverse that requirement — the deliverable of the archived `security-events-unify` change — to do so. Moving `sdf_app` off the dispatch task removes the emit-from-dispatch violation without touching the audit transport at all, which is the main reason this shape was chosen over that one.

## Impact

**Code**

- `sdf_app.c`: new task, queue, and trampoline callback; the three existing subscriber functions become task-loop handlers; nine `sdf_event_router_subscribe()` call sites repointed; task creation ordered after the last subscription and before `sdf_event_router_start()`; watchdog registration via `sdf_platform_time_wdt_add()`; `s_zigbee_alarm_mask` and its twenty call sites.
- `sdf_app.h` / component config: `SDF_APP_TASK_STACK`, `SDF_APP_TASK_PRIORITY`, `SDF_APP_EVENT_QUEUE_DEPTH`.
- `sdf_event_router.h` / `.c`: emit contract and the `send_timeout_ms` doc comment, which currently explains ISR and timer contexts but says nothing about dispatch.
- **Not touched**: `sdf_event_router_capacity.h` (subscription count is unchanged at 9 for `sdf_app`, 21 total), the `sdf_event_router_type_t` enum, the event union, and every audit producer outside `sdf_app` (`sdf_services.c:226, 1095`, `sdf_ota.c:87, 531`, `sdf_services_mock_linux.c:18`). The enum-renumbering and capacity-count risks that an audit-removal approach would carry do not arise.
- `doc/rtos_tasks.md`: four missing tasks added, the new app task added, totals recomputed.

**Tests**

- `test_sdf_event_router.c` asserts that re-entrant emit is safe; that assertion inverts. New coverage: emit-from-callback rejection at every priority and timeout, and the regression case that an emit from a *non*-dispatch task during a dispatch is still accepted.
- Host coverage for the app task mirroring the existing service-task tests: trampoline delivery, drop-under-backpressure, watchdog registered while running.

**Verification**

- `esp-emu` cannot reach the deep handler path organically — no fingerprint sensor is modelled, no GPIO button press can be injected, the device advertises allow-list-filtered with an empty list so no central can connect, and Zigbee steering fails. The stack probe therefore depends on temporary instrumentation that seeds state via `sdf_services_set_web_reg_auth()` and emits the event from `main`, following the precedent in `archive/2026-08-17-unify-event-router-emit` task 4.4. Per project history, any emulator panic is treated as a real defect, not a fidelity artifact.
