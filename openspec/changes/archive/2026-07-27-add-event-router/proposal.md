# Proposal: Add Central Event Router Component

## Summary

Introduce a new `sdf_event_router` component that provides a central event bus with typed events, priority levels, and synchronous/asynchronous dispatch. This replaces ad-hoc callback chains with a proper event-driven architecture.

## Problem

Current architecture uses direct callback registration:
- `sdf_app` registers callbacks with `sdf_services` (unlock_cb, enrollment_cb, admin_action_cb, security_event_cb)
- `sdf_services` calls `sdf_app` functions directly for enrollment/permission changes
- No central event ordering or priority handling
- Difficult to add cross-cutting concerns (logging, metrics, audit)
- Testing requires complex mock setup

## Solution

Create `sdf_event_router` component with:
- **Event types**: Biometric, Zigbee, BLE, Power, Security, Admin, System
- **Priority levels**: CRITICAL (security), HIGH (lock actions), NORMAL (enrollment), LOW (telemetry)
- **Dispatch modes**: Synchronous (inline), Asynchronous (FreeRTOS queue), Deferred (timer)
- **Subscription**: Component-level with filters (event type, priority, source)
- **Built-in audit logging**: All events automatically logged to audit trail

## Architecture Impact

### New Component
```
sdf_event_router/
├── include/sdf_event_router.h
├── src/sdf_event_router.c
└── test/test_sdf_event_router.c
```

### Modified Components
| Component | Changes |
|-----------|---------|
| `sdf_app` | Replace direct callbacks with event subscriptions |
| `sdf_services` | Emit events instead of calling callbacks |
| `sdf_protocol_zigbee` | Emit Zigbee command events |
| `sdf_protocol_ble` | Emit BLE connection/state events |
| `sdf_power` | Emit sleep/wake/battery events |

## API Design

```c
// Event types
typedef enum {
  SDF_EVT_BIOMETRIC_MATCH,
  SDF_EVT_BIOMETRIC_MATCH_FAILED,
  SDF_EVT_ZIGBEE_COMMAND,
  SDF_EVT_BLE_LOCK_ACTION_COMPLETE,
  SDF_EVT_POWER_SLEEP,
  SDF_EVT_POWER_WAKE,
  SDF_EVT_SECURITY_LOCKOUT,
  SDF_EVT_ADMIN_ACTION_REQUEST,
  SDF_EVT_ENROLLMENT_STEP_COMPLETE,
} sdf_event_type_t;

// Priority levels
typedef enum {
  SDF_EVT_PRIO_CRITICAL = 0,  // Security, lockout
  SDF_EVT_PRIO_HIGH = 1,      // Lock/unlock actions
  SDF_EVT_PRIO_NORMAL = 2,    // Enrollment, queries
  SDF_EVT_PRIO_LOW = 3,       // Battery, telemetry
} sdf_event_priority_t;

// Core API
esp_err_t sdf_event_router_init(void);
esp_err_t sdf_event_router_subscribe(sdf_event_type_t type, sdf_event_priority_t min_prio, 
                                     sdf_event_cb cb, void *ctx);
esp_err_t sdf_event_router_emit(const sdf_event_t *event);
esp_err_t sdf_event_router_emit_async(const sdf_event_t *event);  // queued
```

## Benefits

1. **Decoupling**: Components don't know about each other
2. **Priority ordering**: Security events always processed first
3. **Observability**: Central point for logging, metrics, debugging
4. **Testability**: Easy to inject test events, verify emissions
5. **Extensibility**: New event types without modifying existing components
6. **Audit trail**: Automatic event logging for compliance

## Migration Plan

1. Add `sdf_event_router` component with tests
2. Migrate `sdf_services` → emit events instead of callbacks
3. Migrate `sdf_app` → subscribe to events
4. Migrate Zigbee/BLE/Power components
5. Remove old callback infrastructure
6. Update documentation (sdf_sas.md sections 5, 6, 8, 9)

## Risks

- **Performance**: Event queue adds latency (~100-500µs). Mitigation: sync dispatch for critical path.
- **Memory**: Queue depth configuration needed. Mitigation: configurable ring buffer.
- **Migration scope**: Touches 5 components. Mitigation: phase incrementally with adapter layer.

## Acceptance Criteria

- [ ] All biometric unlock events route through event router
- [ ] Zigbee commands emit events, not callbacks
- [ ] Security lockout is CRITICAL priority, processed before lock actions
- [ ] Unit tests cover subscribe/emit/filter/priority ordering
- [ ] Integration test: fingerprint match → unlock event → BLE action
- [ ] Documentation updated per AGENTS.md