## Context

The current architecture uses direct callback registration between `sdf_app` and `sdf_services`:

- `sdf_services_config_t` contains 4 callback pointers: `unlock_cb`, `enrollment_cb`, `admin_action_cb`, `security_event_cb`
- `sdf_app` registers callbacks at init time
- Events flow directly from `sdf_services` to `sdf_app` without central coordination
- No priority ordering or cross-cutting concern support

This creates tight coupling and makes testing difficult. ESP-IDF provides FreeRTOS queues and timers for async dispatch, enabling a proper event bus.

## Goals / Non-Goals

**Goals:**
- Central event bus with typed events and priority levels
- Synchronous dispatch for critical/security events (minimal latency)
- Asynchronous dispatch via FreeRTOS queue for non-critical events
- Subscription API with optional filters (event type, priority min)
- Built-in audit logging integration
- Backward compatibility during migration

**Non-Goals:**
- Do NOT modify existing event payloads initially (adapter pattern)
- Do NOT implement event persistence across reboots
- Do NOT add event filtering by source in v1

## Decisions

**Decision: Separate component (sdf_event_router)**
Rationale: Clean separation, reusable across components, follows existing codebase pattern.
Alternative: Extend sdf_services - rejected due to mixing concerns.

**Decision: Ring buffer queue (depth 32)**
Rationale: ESP32-C6 has sufficient RAM, bounded queue prevents memory exhaustion.
Alternative: Unbounded queue - rejected due to memory risk.

**Decision: Synchronous dispatch for CRITICAL priority**
Rationale: Security events must not be delayed by queue processing.
Async dispatch used for HIGH/NORMAL/LOW priorities.

**Decision: Event struct contains header + payload union**
Rationale: Type safety with size efficiency. Each type has specific payload struct.
Alternative: Void* payload - rejected for type safety concerns.

**Decision: Subscription API returns handle for unsubscribe**
Rationale: Clean unregistration, minimal memory overhead.
Future: Could add wildcard subscriptions if needed.

## Risks / Trade-offs

**Performance risk**: Event queue adds ~100-500µs latency
Mitigation: CRITICAL events use sync dispatch, other priorities can use deferred mode

**Memory risk**: Queue depth consumes ~1KB RAM (32 events × ~32 bytes)
Mitigation: Configurable at Kconfig, default 32 entries

**Migration complexity risk**: 5 components need updates
Mitigation: Adapter layer in sdf_services preserves callback API temporarily

## Migration Plan

1. Add `sdf_event_router` component with API implementation
2. Add Kconfig options: `SDF_EVENT_ROUTER_QUEUE_DEPTH`, `SDF_EVENT_ROUTER_ENABLE_AUDIT`
3. Update `sdf_services` to emit events alongside existing callbacks (adapter mode)
4. Migrate `sdf_app` to subscribe to events, deprecate callbacks
5. Migrate `sdf_protocol_zigbee` to emit Zigbee command events
6. Migrate `sdf_protocol_ble` to emit connection/state events
7. Migrate `sdf_power` to emit sleep/wake/battery events
8. Remove callback infrastructure from `sdf_services` after all consumers migrated
9. Update documentation (doc/sdf_sas.md, doc/user_manual.md)

## Open Questions

- Should audit logging be optional? (Probably yes - Kconfig)
- Do we need event payload size limits? (Design uses 16-byte payload max)
- Priority inheritance for nested events? (Deferred for now)