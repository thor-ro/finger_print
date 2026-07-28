# Proposal: Refactor sdf_services Monolithic Task

## Summary

Split the single `sdf_services_task` (165 lines, cognitive complexity 33) into multiple focused FreeRTOS tasks communicating via the event router. Each task handles one responsibility: match cycle, enrollment execution, admin auth, button handling.

## Problem

Current `sdf_services_task` does everything:
- Fingerprint sensor power management
- Boot-time sensor probe & user query
- Match polling cycle (400ms)
- Enrollment step execution
- Admin authentication cycle (10s timeout)
- Button press handling with debounce
- Security lockout tracking
- LED state management

**Issues:**
- 165 lines, cognitive complexity 33 (highest in codebase)
- Two nested loops with complex state logic
- Blocking operations (fp_probe up to 12s) block all other work
- Difficult to test individual behaviors
- No priority separation - security events compete with LED updates
- Wake/sleep logic intertwined with business logic

## Solution

Split into 4 tasks + 1 event router:

### Task Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    sdf_event_router                         │
│  (Priority: CRITICAL > HIGH > NORMAL > LOW)                │
└──────────┬──────────────┬──────────────┬────────────────────┘
           │              │              │
    ┌──────▼──────┐ ┌─────▼─────┐ ┌─────▼─────┐
    │ sdf_match   │ │ sdf_enroll│ │ sdf_admin │
    │ _task       │ │ _task     │ │ _task     │
    │ (HIGH)      │ │ (NORMAL)  │ │ (HIGH)    │
    └─────────────┘ └───────────┘ └───────────┘
           │              │              │
    ┌──────▼──────────────▼──────────────▼──────┐
    │           sdf_button_task                 │
    │              (NORMAL)                     │
    └───────────────────────────────────────────┘
```

### Task Responsibilities

| Task | Priority | Core Loop | Events Consumed | Events Emitted |
|------|----------|-----------|-----------------|----------------|
| `sdf_match_task` | HIGH | 400ms poll | BIOMETRIC_MATCH_REQUEST | BIOMETRIC_MATCH, BIOMETRIC_MATCH_FAILED |
| `sdf_enroll_task` | NORMAL | Event-driven | ENROLLMENT_START, ENROLLMENT_STEP_RESULT | ENROLLMENT_STEP_COMPLETE, ENROLLMENT_COMPLETE |
| `sdf_admin_task` | HIGH | Event-driven | ADMIN_ACTION_REQUEST, BIOMETRIC_MATCH (admin) | ADMIN_ACTION_COMPLETE, ADMIN_AUTH_RESULT |
| `sdf_button_task` | NORMAL | GPIO ISR + timer | GPIO interrupt | ADMIN_ACTION_REQUEST, BUTTON_PRESS |

### Power Management
- Separate `sdf_power_task` (already exists) handles sleep/wake
- Services tasks receive `POWER_WAKE` / `POWER_SLEEP` events
- Sensor power gating moved to `sdf_drivers` via events

## API Changes

### New Task Creation API
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

### Event Types Added
```c
// In sdf_common.h
typedef enum {
  // Match cycle
  SDF_EVT_BIOMETRIC_MATCH_REQUEST,    // Trigger match cycle
  SDF_EVT_BIOMETRIC_MATCH,            // Match succeeded
  SDF_EVT_BIOMETRIC_MATCH_FAILED,     // Match failed
  
  // Enrollment
  SDF_EVT_ENROLLMENT_START,           // Begin enrollment
  SDF_EVT_ENROLLMENT_STEP_RESULT,     // Driver step result
  SDF_EVT_ENROLLMENT_STEP_COMPLETE,   // Step done
  SDF_EVT_ENROLLMENT_COMPLETE,        // All 3 steps done
  SDF_EVT_ENROLLMENT_FAILED,          // Enrollment failed
  
  // Admin
  SDF_EVT_ADMIN_ACTION_REQUEST,       // Button pressed
  SDF_EVT_ADMIN_AUTH_RESULT,          // Admin match result
  SDF_EVT_ADMIN_ACTION_COMPLETE,      // Action executed
  
  // Button
  SDF_EVT_BUTTON_PRESS,               // Short press
  SDF_EVT_BUTTON_LONG_PRESS,          // Long press
  SDF_EVT_BUTTON_MULTI_PRESS,         // Double/triple
  
  // Power (consumed)
  SDF_EVT_POWER_WAKE,                 // Wake from sleep
  SDF_EVT_POWER_SLEEP,                // Enter sleep
} sdf_event_type_t;
```

## Migration Strategy

### Phase 1: Event Router Foundation (Change: add-event-router)
- Create `sdf_event_router` component
- Define all event types in `sdf_common.h`

### Phase 2: Task Split (This Change)
- Create 4 new task functions in `sdf_services/src/`
- Migrate logic from `sdf_services_task` to respective tasks
- Replace direct function calls with event emissions
- Keep `sdf_services_task` as thin adapter during transition

### Phase 3: Component Integration
- Update `sdf_app` to use event subscriptions
- Update `sdf_protocol_zigbee` to emit command events
- Update `sdf_protocol_ble` to emit BLE state events

### Phase 4: Cleanup
- Remove `sdf_services_task` and old callback infrastructure
- Remove `sdf_services_admin_action_cb` etc. from config
- Update tests

## Benefits

1. **Separation of Concerns**: Each task has single responsibility
2. **Priority Isolation**: Security (admin) runs at HIGH, enrollment at NORMAL
3. **Testability**: Each task testable in isolation with event injection
4. **Responsiveness**: Blocking fp_probe doesn't block match cycle
5. **Power Efficiency**: Tasks can suspend independently when idle
6. **Observability**: Event router provides central logging/metrics

## Risks

- **Stack usage**: 4 tasks × ~4KB = 16KB vs 1 task × 8KB. Mitigation: tune stack sizes.
- **Event latency**: Async dispatch adds ~100-500µs. Mitigation: sync dispatch for CRITICAL.
- **Migration complexity**: Touching core fingerprint logic. Mitigation: comprehensive tests first.

## Acceptance Criteria

- [ ] 4 tasks created with correct priorities
- [ ] Match cycle runs independently at 400ms
- [ ] Enrollment executes steps via events
- [ ] Admin auth waits for fingerprint match via events
- [ ] Button presses emit correct events with debounce
- [ ] All existing unit tests pass
- [ ] Integration test: fingerprint → unlock → BLE action
- [ ] Memory usage within budget (< 20KB total task stacks)
- [ ] Documentation updated (sdf_sas.md sections 5, 6)