## 1. Capacity declaration

- [x] 1.1 Add `firmware/components/sdf_event_router/include/sdf_event_router_capacity.h` declaring per-component subscription counts (`SDF_EVENT_ROUTER_SUBS_APP` 9, `_SERVICES_MATCH` 3, `_SERVICES_ADMIN` 4, `_SERVICES_ENROLL` 3, `_BLE_COMPANION` 2) and `SDF_EVENT_ROUTER_SUBS_DECLARED_TOTAL` as their sum
- [x] 1.2 Define `SDF_EVENT_ROUTER_SUBS_HEADROOM` as `0` with a comment explaining that a non-zero value makes the declared counts unenforceable, and define `SDF_EVENT_ROUTER_SUBSCRIBER_CAPACITY` as the declared total plus headroom — sizing the pool exactly at the declared total
- [x] 1.3 Add `static_assert` that the declared total fits the capacity and that the capacity is representable in the `uint8_t` slot index (`< 0xFF`)

## 2. Router internals and public API

- [x] 2.1 Replace the per-type linked lists in `sdf_event_router.c` with a static `s_pool[SDF_EVENT_ROUTER_SUBSCRIBER_CAPACITY]` (fields: `uint8_t type`, `uint8_t min_prio`, `cb`, `ctx`, `uint8_t next`) plus `uint8_t s_head_by_type[SDF_EVENT_ROUTER_TYPE_COUNT]` initialised to the `0xFF` end sentinel
- [x] 2.2 Change `sdf_event_router_subscribe()` to claim a pool slot with no heap allocation, drop the `handle` out-parameter from the signature in `sdf_event_router.h`, and return `ESP_ERR_INVALID_STATE` when the router is already started and `ESP_ERR_NO_MEM` when the pool is exhausted (incrementing a rejected-registration counter in that case)
- [x] 2.3 Remove `sdf_event_router_unsubscribe()` and the `sdf_event_router_subscriber_t` typedef from the public header and the implementation
- [x] 2.4 Move dispatch task creation out of `sdf_event_router_init()` into a new `sdf_event_router_start()` that sets the started flag, returns `ESP_ERR_INVALID_STATE` if init has not run, is a no-op returning `ESP_OK` on repeat calls, fails if the rejected-registration counter is non-zero, and logs registered-vs-capacity on success
- [x] 2.5 Rewrite `sdf_event_router_dispatch_sync()` to walk the frozen index chain directly — removing the `xSemaphoreTake`/`Give` pair, the `cbs`/`ctxs` snapshot arrays, and `SDF_EVENT_ROUTER_MAX_DISPATCH` — while keeping the existing event-type validation ahead of the table index
- [x] 2.6 Delete `s_state.lock` and its creation/teardown paths in `init()` now that no code path takes it
- [x] 2.7 Document the `min_prio` ordering in `sdf_event_router.h` next to the enum: it is the lowest importance accepted, evaluated as `min_prio >= event->priority`, so `PRIO_CRITICAL` admits only critical events
- [x] 2.8 Add a test-target-only `sdf_event_router_reset_for_test()` (guarded so it is not built for the device target) that clears the pool, head table, started flag, and rejected counter

## 3. Service task registration relocation

- [x] 3.1 Move `sdf_match_task_init_subscriptions()` out of the match task body (`sdf_services_match.c:245`) and call it from `sdf_services_init()` after the shared task queues are created and before `sdf_services_start_tasks()`
- [x] 3.2 Do the same for `sdf_admin_task_init_subscriptions()` (`sdf_services_admin.c:182`) and `sdf_enroll_task_init_subscriptions()` (`sdf_services_enroll.c:268`)
- [x] 3.3 Delete `sdf_match_task_deinit_subscriptions()`, `sdf_admin_task_deinit_subscriptions()`, and `sdf_enroll_task_deinit_subscriptions()`, keeping the `state->event_queue = NULL` assignment in each task's shutdown path so post-exit callbacks discard events
- [x] 3.4 Remove the subscriber handle fields (`sub_match_req`, `sub_power_wake`, `sub_power_sleep`, and the admin/enroll equivalents) from the per-task state structs
- [x] 3.5 Add a comment at `sdf_services_stop_tasks()` recording that subscriptions are permanent for the boot, so any future revival of this uncalled path cannot assume a clean subscriber teardown

## 4. Application and BLE companion call sites

- [x] 4.1 Update the 9 `sdf_app_init()` subscriptions (`sdf_app.c:1702–1788`) to the handle-free signature and delete the `subs[]` array and the `sub_cleanup` rollback label
- [x] 4.2 **Bugfix**: change the `SECURITY_LOCKOUT` subscription (`sdf_app.c:1711`) from `SDF_EVENT_ROUTER_PRIO_CRITICAL` to `SDF_EVENT_ROUTER_PRIO_NORMAL` so the NORMAL "lockout cleared" emission is delivered, with a comment explaining the inverted-enum filter
- [x] 4.3 Update the 2 `sdf_ble_companion_init()` subscriptions (`sdf_ble_companion.c:1224`, `:1232`), remove `s_enrollment_sub` / `s_enrollment_failed_sub`, and drop the unsubscribe calls from `sdf_ble_companion_deinit()` (`:1278`, `:1282`) with the same permanence comment as 3.5
- [x] 4.4 Call `sdf_event_router_start()` in `sdf_app_init()` immediately after `sdf_ble_companion_init()` (`sdf_app.c:1827`), treating a failure as an init error, with a comment that no subscriber may register past this point

## 5. Tests

- [x] 5.1 Rewrite `test_sdf_event_router.c` around the init → subscribe → start → emit lifecycle, calling `sdf_event_router_reset_for_test()` between cases
- [x] 5.2 Delete `test_sdf_event_router_unsubscribe()` and replace it with a case asserting `sdf_event_router_subscribe()` returns `ESP_ERR_INVALID_STATE` after `start()`
- [x] 5.3 Add a case asserting every matching subscriber is invoked when more than one is registered for a type, with no fan-out truncation
- [x] 5.4 Add a case asserting `min_prio` filter semantics in both directions: a `PRIO_LOW` subscriber receives a CRITICAL event, and a `PRIO_CRITICAL` subscriber does not receive a NORMAL event
- [x] 5.5 Add a case asserting a callback that emits a `PRIO_CRITICAL` event during dispatch completes without deadlock
- [x] 5.6 Add a case covering the lockout pair: a subscriber registered as `sdf_app` registers receives both the CRITICAL entered and NORMAL cleared `SECURITY_LOCKOUT` emissions
- [x] 5.7 Update any `sdf_services` or BLE companion tests that referenced subscriber handles or subscription teardown

## 6. Verification

- [x] 6.1 Build the device target and confirm no remaining references to `sdf_event_router_unsubscribe`, `sdf_event_router_subscriber_t`, or `SDF_EVENT_ROUTER_MAX_DISPATCH`
- [x] 6.2 Run the host test runner (`build_linux`) green
- [ ] 6.3 Flash and confirm the boot log shows `sdf_event_router_start()` reporting 21 registered subscribers against capacity, with no `ESP_ERR_INVALID_STATE` registration rejections
- [ ] 6.4 On hardware, drive a lockout cycle (exceed the failed-attempt threshold, then wait out the lockout) and confirm the Zigbee biometric lockout alarm bit clears and a `BIOMETRIC_LOCKOUT_CLEARED` audit entry is written
- [ ] 6.5 Confirm boot-time emissions do not overflow the router queue during the init→start window by checking for "Event queue full" warnings in the boot log
- [x] 6.6 Run `openspec validate --strict static-event-router-subscriptions`
