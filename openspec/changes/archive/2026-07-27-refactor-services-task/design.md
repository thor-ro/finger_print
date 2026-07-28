# Design: Refactor sdf_services Monolithic Task

## Context

The current `sdf_services_task` is a 165-line FreeRTOS task with cognitive complexity of 33 (highest in the codebase). It handles:
- Fingerprint sensor power management
- Boot-time sensor probe & user query
- Match polling cycle (400ms)
- Enrollment step execution
- Admin authentication cycle (10s timeout)
- Button press handling with debounce
- Security lockout tracking
- LED state management

Issues:
- Two nested loops with complex state logic
- Blocking operations (fp_probe up to 12s) block all other work
- Difficult to test individual behaviors
- No priority separation - security events compete with LED updates
- Wake/sleep logic intertwined with business logic

The `sdf_event_router` component already exists and provides priority-based event dispatch.

## Goals / Non-Goals

**Goals:**
- Split monolithic task into 4 focused tasks with clear responsibilities
- Use event router for inter-task communication
- Separate priorities: security (HIGH) vs enrollment (NORMAL)
- Enable independent testing of each task
- Allow tasks to suspend independently for power efficiency
- Keep `sdf_services_task` as thin adapter during transition

**Non-Goals:**
- Full component integration (Phase 3: sdf_app, zigbee, BLE updates)
- Cleanup of old callback infrastructure (Phase 4)
- New component creation (sdf_event_router already exists in Phase 1)

## Decisions

### 1. Task Architecture (4 tasks + 1 event router)

| Task | Priority | Stack | Core Loop | Events Consumed | Events Emitted |
|------|----------|-------|-----------|-----------------|----------------|
| `sdf_match_task` | HIGH (6) | 4KB | 400ms poll | BIOMETRIC_MATCH_REQUEST, POWER_WAKE | BIOMETRIC_MATCH, BIOMETRIC_MATCH_FAILED, SECURITY_LOCKOUT |
| `sdf_enroll_task` | NORMAL (5) | 4KB | Event-driven | ENROLLMENT_START, ENROLLMENT_STEP_RESULT, POWER_WAKE | ENROLLMENT_STEP_COMPLETE, ENROLLMENT_COMPLETE, ENROLLMENT_FAILED |
| `sdf_admin_task` | HIGH (6) | 4KB | Event-driven | ADMIN_ACTION_REQUEST, BIOMETRIC_MATCH (admin), POWER_WAKE | ADMIN_ACTION_COMPLETE, ADMIN_AUTH_RESULT |
| `sdf_button_task` | NORMAL (5) | 3KB | GPIO ISR + timer | GPIO interrupt | ADMIN_ACTION_REQUEST, BUTTON_PRESS |

**Rationale:** 
- Security-critical paths (match, admin) run at HIGH priority
- Enrollment and button handling run at NORMAL
- Stack sizes tuned: 4KB for tasks with fp_match_1n() blocking calls, 3KB for button
- Event router runs at CRITICAL priority for dispatch

### 2. Event Type Extension

New event types added to `sdf_common.h` (not sdf_event_router.h) to keep domain events with their domain:
```c
// Match cycle
SDF_EVT_BIOMETRIC_MATCH_REQUEST    // Trigger match cycle
SDF_EVT_BIOMETRIC_MATCH            // Match succeeded
SDF_EVT_BIOMETRIC_MATCH_FAILED     // Match failed

// Enrollment
SDF_EVT_ENROLLMENT_START           // Begin enrollment
SDF_EVT_ENROLLMENT_STEP_RESULT     // Driver step result
SDF_EVT_ENROLLMENT_STEP_COMPLETE   // Step done
SDF_EVT_ENROLLMENT_COMPLETE        // All 3 steps done
SDF_EVT_ENROLLMENT_FAILED          // Enrollment failed

// Admin
SDF_EVT_ADMIN_ACTION_REQUEST       // Button pressed
SDF_EVT_ADMIN_AUTH_RESULT          // Admin match result
SDF_EVT_ADMIN_ACTION_COMPLETE      // Action executed

// Button
SDF_EVT_BUTTON_PRESS               // Short press
SDF_EVT_BUTTON_LONG_PRESS          // Long press
SDF_EVT_BUTTON_MULTI_PRESS         // Double/triple

// Power (consumed by all tasks)
SDF_EVT_POWER_WAKE                 // Wake from sleep
SDF_EVT_POWER_SLEEP                // Enter sleep
```

**Rationale:** Keep domain-specific events in their domain headers. sdf_event_router only handles cross-cutting events.

### 3. State Management

Shared state (`s_state`) remains protected by mutex. Each task accesses only its relevant fields. New per-task state:
- `sdf_match_task`: match_cooldown_until_us, failed_attempt_count, lockout_until_us
- `sdf_enroll_task`: enrollment SM, enrollment_request_pending, request_user_id, request_permission
- `sdf_admin_task`: pending_admin_action, pending_admin_action_start_us, admin_action_done_sem
- `sdf_button_task`: button handle, debounce timers

### 4. Migration Strategy

**Phase A (this change):** Create new tasks, migrate logic, keep old task as adapter
- Add `sdf_services_start_tasks()` / `sdf_services_stop_tasks()` API
- Create 4 new task functions in `sdf_services/src/sdf_services_tasks.c`
- Migrate logic from `sdf_services_task` to respective tasks
- Replace direct function calls with event emissions
- Keep `sdf_services_task` running but delegating to events

**Phase B (future):** Remove old task, update components
- Update `sdf_app` to use event subscriptions
- Update `sdf_protocol_zigbee` to emit command events
- Update `sdf_protocol_ble` to emit BLE state events
- Remove `sdf_services_task` and old callback infrastructure

### 5. Power Management Integration

- `sdf_power_task` (existing) handles sleep/wake
- Services tasks subscribe to `POWER_WAKE` / `POWER_SLEEP`
- Sensor power gating moved to `sdf_drivers` via events
- Each task can suspend independently when idle

### 6. Task Creation API

```c
// In sdf_services.h
esp_err_t sdf_services_start_tasks(void);  // Creates all 4 tasks
esp_err_t sdf_services_stop_tasks(void);   // Deletes all 4 tasks

// Internal task functions (not public)
void sdf_match_task(void *arg);
void sdf_enroll_task(void *arg);
void sdf_admin_task(void *arg);
void sdf_button_task(void *arg);
```

## Risks / Trade-offs

| Risk | Impact | Mitigation |
|------|--------|------------|
| Stack usage: 4 tasks × ~4KB = 16KB vs 1 task × 8KB | Medium | Tune stack sizes; monitor with `uxTaskGetStackHighWaterMark()` |
| Event latency: Async dispatch adds ~100-500µs | Low | Sync dispatch for CRITICAL priority events |
| Migration complexity: Touching core fingerprint logic | High | Comprehensive tests first; keep old task as adapter |
| Shared state contention on mutex | Medium | Minimize critical sections; use lock-free where possible |
| Task synchronization bugs | High | Clear ownership of state fields per task; document invariants |

## Migration Plan

1. Create design.md ✓
2. Create specs for new event types and task behaviors
3. Create tasks.md with implementation checklist
4. Add new event types to `sdf_common.h`
5. Create `sdf_services_tasks.c` with 4 new task functions
6. Add `sdf_services_start_tasks()` / `sdf_services_stop_tasks()` to `sdf_services.c`
7. Migrate match cycle logic to `sdf_match_task`
8. Migrate enrollment logic to `sdf_enroll_task`
9. Migrate admin auth logic to `sdf_admin_task`
10. Migrate button handling to `sdf_button_task`
10. Update `sdf_services_task` to delegate via events
11. Run unit tests
12. Run integration test on hardware

## Open Questions

1. Should `sdf_match_task` use `vTaskDelay()` or event-driven polling or timer-based?
   - Current: `vTaskDelay(poll_interval_ms)` - simple, accurate enough
   - Alternative: ESP timer + event - more complex, better for deep sleep sync

2. How to handle button debounce without iot_button?
   - Current: iot_button callbacks on GPIO ISR
   - Option A: Keep iot_button, emit events from callbacks
   - Option B: Implement simple debounce in `sdf_button_task` loop

3. Should enrollment SM state move to `sdf_enroll_task` or stay in shared state?
   - Current: SM in shared state (`s_state.enrollment`)
   - Option A: Move SM instance to task-local, only results shared
   - Option B: Keep in shared state with mutex (current approach)