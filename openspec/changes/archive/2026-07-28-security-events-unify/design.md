# Design: Unify Dual-Path Security Event Emission

## Context

The `sdf_services` component currently emits security events through two parallel paths:
1. Legacy callback (`sdf_services_notify_security_event()`) — calls `security_event_cb` registered in `sdf_app`
2. Event router emission (`sdf_match_task_notify_security_event()`) — emits typed event via `sdf_event_router_emit()`

The legacy callback path was the original mechanism before the event router was introduced. After the event router merge, both paths coexist, causing duplicated audit entries.

## Decision

Remove the legacy callback path entirely. Route all security events exclusively through the event router.

## Rationale

1. **Single emission path**: Eliminates the dual-path maintenance burden
2. **Consistent audit logging**: The event router's built-in audit logging handles all security events uniformly
3. **Simpler subscription model**: `sdf_app` subscribes to `SDF_EVENT_ROUTER_SECURITY_LOCKOUT` (CRITICAL) and `SDF_EVENT_ROUTER_BIOMETRIC_MATCH` (HIGH) events
4. **No functional loss**: All current subscribers (sdf_app, sdf_event_router audit) continue to receive events

## Implementation Plan

### Phase 1: Add Event Router Subscriptions in sdf_app
- Subscribe to `SDF_EVENT_ROUTER_BIOMETRIC_MATCH_FAILED` at HIGH priority
- Subscribe to `SDF_EVENT_ROUTER_SECURITY_LOCKOUT` at CRITICAL priority
- Route `sdf_app_on_security_event()` logic through these subscriptions

### Phase 2: Remove Legacy Callback from sdf_services
- Remove `sdf_services_notify_security_event()`:
  - sdf_services.c calls this from `sdf_services_run_match_cycle()` for match success/fail/lockout
  - Replace with direct event router emission
- Remove `sdf_services_security_event_cb` from config struct
- Remove callback invocation code

### Phase 3: Remove Task-Level Duplication
- Remove `sdf_match_task_notify_security_event()` from `sdf_services_match.c`
- The match cycle in `sdf_services_match.c` already emits events to the event router directly for some cases; ensure all paths use the same pattern

### Phase 4: Update sdf_app
- Remove `sdf_app_on_security_event()` callback registration from services config
- Keep the logic that was in `sdf_app_on_security_event()` (alarm mask, audit) but route through event router subscription

## Risk Mitigation

- **Low risk**: The event router already handles security events correctly; we're just removing the duplicate path
- **Test coverage**: Existing event router tests cover emission; verify the legacy path doesn't fire anymore
- **Backward compatibility**: If any external code uses `sdf_services_security_event_cb`, update docs to use event router subscriptions instead