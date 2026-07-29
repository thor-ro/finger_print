## 1. Power Management: Light-Sleep Dedup & Deep Sleep Retention Fix

- [x] 1.1 Extract `sdf_power_enter_light_sleep()` helper from duplicated code in `sdf_power_sleep_once()` and `sdf_power_task()`
- [x] 1.2 Refactor `sdf_power_sleep_once()` to call the new helper
- [x] 1.3 Refactor `sdf_power_task()` light-sleep path to call the new helper
- [x] 1.4 Replace manual retention write in `sdf_power_task()` deep sleep path with `sdf_power_save_retention()` call
- [x] 1.5 Wire `sdf_power_prepare_deep_sleep()` stub fields to real state getters (last_activity_us, next_checkin_us, etc.)

## 2. Power Management: Event Type Separation & Locking Fix

- [x] 2.1 Add `SDF_EVENT_ROUTER_POWER_BATTERY` to `sdf_event_router_type_t` enum in `sdf_event_router.h`
- [x] 2.2 Change `sdf_power_push_battery_percent()` to emit `SDF_EVENT_ROUTER_POWER_BATTERY` instead of `SDF_EVENT_ROUTER_POWER_SLEEP`
- [x] 2.3 Consolidate power task locking in `sdf_power_task()` to use a single lock/unlock cycle per iteration

## 3. Event Router: Type-Indexed Dispatch & Async Priority Fix

- [x] 3.1 Add type-indexed subscriber array to `sdf_event_router_state_t` (array of head pointers, one per event type)
- [x] 3.2 Update `sdf_event_router_subscribe()` to prepend to the type-indexed list
- [x] 3.3 Update `sdf_event_router_unsubscribe()` to remove from the type-indexed list
- [x] 3.4 Update `sdf_event_router_dispatch_sync()` to use the type index instead of walking all subscribers
- [x] 3.5 Fix `sdf_event_router_emit_async()` to route `PRIO_CRITICAL` events synchronously

## 4. Match Task: Lock Consolidation & Deduplication

- [x] 4.1 Consolidate lock acquisitions in `sdf_match_task_run_match_cycle()` — read all config/state in one lock/unlock, release before `fp_match_1n()`, then acquire once at the end for state writes
- [x] 4.2 Extract lockout-cleared event emission to a helper function and call it from a single shared location

## 5. Match Task: Suspend/Resume Mechanism

- [x] 5.1 Replace WDT delete/recreate + `portMAX_DELAY` semaphore-block with a suspend flag in the match task main loop
- [x] 5.2 Increase poll interval to 10 seconds (or power check-in interval) when suspended
- [x] 5.3 Verify `POWER_WAKE` and `BIOMETRIC_MATCH_REQUEST` events properly resume the match task from suspended state
- [x] 5.4 Ensure WDT remains active during suspended state

## 6. App Layer: Bug Fixes & Resource Leak Prevention

- [x] 6.1 Remove duplicate `s_pairing_active` and `s_pairing_requested` declarations in `sdf_app.c`
- [x] 6.2 Add subscription cleanup on init failure in `sdf_app_init()` — track all successful subscriptions and unsubscribe on error
- [x] 6.3 Guard alarm mask updates in `sdf_app_set_alarm_mask_bits()` — only call `sdf_protocol_zigbee_update_alarm_mask()` when the mask actually changes

## 7. Enrollment State Machine & Services Cleanup

- [x] 7.1 Refactor `sdf_enrollment_sm_start()` to call `sdf_enrollment_sm_init()` instead of duplicating reset logic
- [x] 7.2 Optimize `sdf_services_start_local_enrollment_with_permission()` free-ID search to iterate enrolled users instead of scanning 1..4096
- [x] 7.3 Document the 3072 bytes of static user query buffers in `sdf_services.c` and evaluate if a compact representation is feasible