## Why

The event router pays for a dynamic subscription capability that nothing uses. All 21 subscriptions in the firmware are registered once during boot with link-time-constant `ctx` values; every `unsubscribe()` call site is either unreachable dead code (`sdf_services_stop_tasks()` and `sdf_ble_companion_deinit()` have zero callers) or runs immediately before `vTaskDelete()`, and real-device teardown goes through `esp_restart()`. In exchange the router carries a heap allocation per subscriber, a mutex on the dispatch path, a bounded stack snapshot to defend against use-after-free, and an `unsubscribe()` that returns `ESP_OK` while callbacks from that subscriber may still be in flight — an unsound contract on a security device.

The same review surfaced a live bug: `sdf_app` subscribes to `SECURITY_LOCKOUT` with `min_prio = PRIO_CRITICAL`, but `min_prio` is a numeric ceiling against an inverted priority enum (`CRITICAL = 0` … `LOW = 3`), so the `PRIO_NORMAL` "lockout cleared" event is silently filtered out. The Zigbee `BIOMETRIC_LOCKOUT` alarm bit latches until reboot and the `BIOMETRIC_LOCKOUT_CLEARED` audit record is never written, violating requirements already stated in `security-event-unification`.

## What Changes

- **BREAKING** Remove `sdf_event_router_unsubscribe()` from the public API. Subscriptions become permanent for the lifetime of the boot.
- Replace the per-type heap-allocated linked lists with a fixed-size static subscriber pool sized at compile time, removing `calloc`/`free` from the router entirely.
- **BREAKING** Split router startup into two phases: `sdf_event_router_init()` prepares the queue and subscriber table, and a new `sdf_event_router_start()` creates the dispatch task. Subscriptions are registered between the two calls; `sdf_event_router_subscribe()` after `start()` is rejected with `ESP_ERR_INVALID_STATE`.
- Make dispatch lock-free. With the subscriber table frozen before the first dispatch can run, the dispatch mutex, the 100 ms lock-acquire timeout (and the event drop it caused), and the `SDF_EVENT_ROUTER_MAX_DISPATCH` stack snapshot all disappear.
- **BREAKING** Drop the `sdf_event_router_subscriber_t **handle` out-parameter from `sdf_event_router_subscribe()`. It existed only to feed `unsubscribe()`; keeping a write-only handle would invite callers to store pointers with no valid use.
- Replace the runtime fan-out cap warning with declared per-component subscription counts, a build-time assertion that the pool covers the declared total, and a startup check that fails if any registration was rejected — instead of a silently truncated subscriber list.
- Move subscription registration in `sdf_services_match`, `sdf_services_admin`, and `sdf_services_enroll` out of the task bodies into `sdf_services_init()`, before tasks are created. This is what makes the frozen-table guarantee real: today those tasks self-register asynchronously and can race a synchronous `PRIO_CRITICAL` dispatch during boot.
- Drop the now-empty subscription-teardown helpers and the `unsubscribe()` calls in `sdf_services_stop_tasks()`, `sdf_ble_companion_deinit()`, and the `sdf_app` init-failure rollback path.
- Fix the `SECURITY_LOCKOUT` subscription in `sdf_app` to accept `PRIO_NORMAL`, restoring delivery of the lockout-cleared event so the Zigbee alarm bit clears and the `BIOMETRIC_LOCKOUT_CLEARED` audit event is logged.
- Document the `min_prio` filter semantics explicitly in the public header and spec: it is the *lowest importance accepted*, implemented as `min_prio >= event->priority` over an inverted enum, so a stricter-looking value admits fewer events.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `sdf-event-router`: `unsubscribe()` is removed; subscriber registration is bounded by a compile-time capacity and permitted only before the router is started; dispatch no longer takes a lock or snapshots subscribers; `min_prio` filter semantics become an explicit requirement.
- `sdf-services-tasks`: match, enroll, and admin tasks no longer subscribe from inside their task bodies or unsubscribe during cooperative shutdown; registration happens during service init before task creation.
- `security-event-unification`: adds the delivery-side requirement that the subscriber for `SECURITY_LOCKOUT` accepts both the CRITICAL "entered" and NORMAL "cleared" emissions, which the existing audit requirements depend on.

## Impact

**Firmware components**

- `sdf_event_router` — `include/sdf_event_router.h` (API removal, new `start()`, capacity macro, documented filter semantics), `src/sdf_event_router.c` (static pool, lock-free dispatch), `test/test_sdf_event_router.c` (unsubscribe tests removed; tests must be reworked around the two-phase lifecycle since `init()` early-returns when already initialized and no longer resets the pool).
- `sdf_app` — 9 subscriptions; the `sub_cleanup` rollback path is removed, and the `SECURITY_LOCKOUT` `min_prio` bugfix lands here. Must call `sdf_event_router_start()` after all subsystems have registered.
- `sdf_services` — `sdf_services.c` (registration ordering relative to `start_tasks()`), `sdf_services_match.c`, `sdf_services_admin.c`, `sdf_services_enroll.c` (3–4 subscriptions each move out of task bodies).
- `sdf_ble_companion` — 2 subscriptions; `deinit()` loses its unsubscribe calls.

**Risk**

- Boot-ordering regression is the main hazard: any subscription that ends up registered after `sdf_event_router_start()` becomes a hard error instead of working by luck. This is intentional — it converts a silent race into a startup failure — but it means the init sequence must be verified on hardware, not only in the host test runner.
- Startup memory becomes fully static: capacity is reserved whether or not every subscriber registers.

**Out of scope**

- Renaming `min_prio` or introducing an `AT_LEAST(x)` helper to fix the inverted-enum ergonomics at the root; this change documents and specifies the semantics instead.
- `sdf_event_router_emit_nonblocking()` exists in the code but the current `sdf-event-router` spec states there SHALL NOT be a second emit entry point. That drift predates this change and is left alone here.
