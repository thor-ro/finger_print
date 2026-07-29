## Why

Code review of the Smart Door Finger firmware revealed multiple optimization opportunities across power management, event routing, fingerprint matching, and app-layer state handling. These optimizations reduce duplicated code, fix correctness risks in deep sleep retention, reduce unnecessary locking and polling overhead, and eliminate a variable-declaration bug. Several items also address power consumption and responsiveness on the ESP32-C6 target.

## What Changes

- **Extract light-sleep helper** (`sdf_power_enter_light_sleep`) to eliminate ~40 lines of duplicated sleep-entry logic between `sdf_power_sleep_once()` and `sdf_power_task()`
- **Fix deep sleep retention** — wire `sdf_power_prepare_deep_sleep()` stub fields to real state getters or replace with `sdf_power_save_retention()` which computes CRC; fix deep sleep path in `sdf_power_task()` to use CRC-protected retention writes
- **Consolidate power-task locking** — use a single lock/unlock cycle per loop iteration instead of inconsistent locking patterns
- **Separate battery event type** — replace misappropriated `SDF_EVENT_ROUTER_POWER_SLEEP` for battery updates with a dedicated `SDF_EVENT_ROUTER_POWER_BATTERY` event type
- **Index event router subscribers by type** — replace O(n) linked-list scan with type-indexed dispatch for O(1) filtering
- **Fix emit_async priority bypass** — document or assert that critical-priority events must use synchronous `emit()`
- **Consolidate match-task lock acquisitions** — reduce 6+ lock/unlock pairs per cycle to 2–3 by batching config reads and state writes
- **Deduplicate lockout-cleared emission** — extract shared emission logic to avoid double-fire risk
- **Replace heavy sleep transition** — replace WDT delete/recreate + semaphore-block in match task with a suspend flag + longer poll interval or power-manager notification
- **Fix duplicate variable declarations** in `sdf_app.c` lines 52–62 (`s_pairing_active` and `s_pairing_requested` declared twice)
- **Add subscription cleanup on init failure** in `sdf_app_init()` to prevent resource leaks and duplicate subscriptions
- **Guard alarm mask updates** — only call `sdf_protocol_zigbee_update_alarm_mask()` when the mask actually changes
- **Consolidate enrollment state-machine re-initialization** — have `sdf_enrollment_sm_start()` call `sdf_enrollment_sm_init()` instead of duplicating reset logic
- **Optimize free-ID search** in `sdf_services_start_local_enrollment_with_permission()` — replace O(4096) scan with O(count) iteration through enrolled user buffer
- **Reduce static buffer footprint** — document or optimize the 3072 bytes of user query buffers in `sdf_services.c`
- **Cache `esp_timer_get_time()`** — avoid redundant calls within the same cycle in power, match, and services tasks

## Capabilities

### New Capabilities
- `power-sleep-dedup`: Extract shared light-sleep entry logic to eliminate code duplication and ensure consistency between one-shot and loop-based sleep paths
- `deep-sleep-retention-fix`: Wire stub fields in `sdf_power_prepare_deep_sleep()` to real state getters; ensure CRC-protected retention writes in deep sleep path
- `event-router-indexing`: Index subscribers by event type for O(1) dispatch; fix async emit to respect priority for critical events
- `match-task-lock-consolidation`: Reduce lock/unlock pairs in match cycle; consolidate lockout-cleared emission logic
- `match-task-suspend-resume`: Replace WDT delete/recreate + semaphore-block with a proper suspend/resume mechanism integrated with the power manager
- `app-layer-bugfixes`: Fix duplicate variable declarations, add subscription cleanup on init failure, guard alarm mask updates
- `enrollment-state-cleanup`: Consolidate re-initialization in enrollment state machine; optimize free-ID search

### Modified Capabilities
- `power-state-consistency` (existing): Fix inconsistent locking in power task config snapshot; add battery event type separation

## Impact

- **Code size**: Reduced duplication (~40 LOC extracted, plus other dedup) saves flash
- **RAM**: Proper deep sleep retention prevents state loss on wake; alarm mask guard prevents unnecessary Zigbee messages
- **Power**: Better sleep/wake transitions; consolidated locking reduces contention overhead; proper suspend/resume replaces heavy WDT teardown
- **Reliability**: Fixed duplicate variable declarations (potential silent bug); subscription cleanup prevents resource leaks; CRC-protected retention prevents silent corruption
- **Responsiveness**: Type-indexed event dispatch reduces emit latency; consolidated locking reduces contention in the match task
- **Build system**: No build configuration changes required