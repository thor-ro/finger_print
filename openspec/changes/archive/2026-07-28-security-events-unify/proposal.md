# Proposal: Unify Dual-Path Security Event Emission

## Summary

Consolidate the two separate security event emission paths into a single unified path through the event router. Currently, security events (match success/fail, lockout entered/cleared) are emitted through both the legacy callback mechanism (`sdf_services_notify_security_event()`) and the event router (`sdf_match_task_notify_security_event()`), creating duplicated audit logging and potential inconsistency.

## Problem

Security events are emitted through two independent code paths:

1. **Legacy path**: `sdf_services.c:sdf_services_notify_security_event()` — calls registered callback AND emits event router event
2. **Task path**: `sdf_services_match.c:sdf_match_task_notify_security_event()` — emits event router event directly

This causes:
- **Duplicate audit entries**: The same security event is logged twice (once via callback in `sdf_app`, once via event router audit logging)
- **Inconsistency risk**: If the callback is not set or fails, the event router path still fires, but with different context
- **Maintenance burden**: Every security event change requires updating both paths
- **Testing complexity**: Must verify both paths emit correctly

## Solution

Remove the legacy callback-based security event path (`sdf_services_notify_security_event()`) and route all security events exclusively through the event router. The `sdf_app` subscribes to security events via `sdf_event_router_subscribe()` and handles audit logging there.

## Architecture Impact

### Remove
- `sdf_services_notify_security_event()` and its helper `sdf_match_task_notify_security_event()`
- `sdf_services_security_event_cb` callback type and `security_event_cb` / `security_event_ctx` config fields

### Simplify
- `sdf_services.c` no longer needs `sdf_audit_cb` for security events (already handled by event router)
- `sdf_app.c` replaces `sdf_app_on_security_event()` callback with event router subscription

### Add
- Event router subscription for `SDF_EVENT_ROUTER_SECURITY_LOCKOUT` in `sdf_app` init
- Event router subscription for `SDF_EVENT_ROUTER_BIOMETRIC_MATCH` (already exists)

## API Design

No new API changes. The existing event router already supports all event types needed:
- `SDF_EVENT_ROUTER_BIOMETRIC_MATCH` (HIGH)
- `SDF_EVENT_ROUTER_BIOMETRIC_MATCH_FAILED` (HIGH)
- `SDF_EVENT_ROUTER_SECURITY_LOCKOUT` (CRITICAL)

## Benefits

1. **Single source of truth**: One emission path per event type
2. **No duplicate audit**: Audit logging happens once per event
3. **Simpler code**: Remove ~80 lines of duplicated event emission logic
4. **Testability**: Only one emission path to verify
5. **Consistency**: All subscribers receive the same event regardless of source

## Acceptance Criteria

- [ ] sdf_services_notify_security_event() removed
- [ ] sdf_match_task_notify_security_event() removed
- [ ] sdf_app_on_security_event() replaced with event router subscription
- [ ] All security events (match success/fail, lockout entered/cleared) emit exactly once
- [ ] Audit counters still increment correctly
- [ ] Zigbee alarm bits still set correctly on lockout
- [ ] Low battery warning still triggers on match success (via event router)
- [ ] Unit tests updated to cover single emission path