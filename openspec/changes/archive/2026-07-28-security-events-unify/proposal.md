## Why

Security events (biometric match, match failure, lockout enter/clear) currently have two emission paths: the event router (`sdf_event_router_emit()`) and legacy callbacks in `sdf_app.c` (`sdf_app_emit_event()`, `sdf_app_emit_audit()`). This dual-path design risks duplicate audit entries and inconsistent event delivery, making it hard to reason about security audit trails. Unifying all security events through the event router eliminates the legacy callback path and ensures exactly-one audit per event.

## What Changes

- Remove the legacy event and audit callback mechanism from `sdf_app.c` (`sdf_app_set_event_callback`, `sdf_app_set_audit_callback`)
- Route all `sdf_app_emit_audit()` calls through the event router via a dedicated audit event type instead of direct callback invocation
- Ensure `sdf_services_emit_security_event()` is the single source of truth for security event emission
- Remove any remaining direct `sdf_app_emit_event()` / `sdf_app_emit_audit()` calls from the security event path in `sdf_app_on_event()` and replace with event-router-first flow
- Update `sdf_app_on_event()` to rely exclusively on event-router-delivered security events

## Capabilities

### New Capabilities
- `security-event-unification`: Remove legacy callback paths and unify all security event emission through `sdf_event_router_emit()`

### Modified Capabilities
- `security-event-emission`: Tighten the requirement — the system SHALL NOT use legacy callbacks for any security event; all must flow through the event router

## Impact

- `firmware/components/sdf_app/src/sdf_app.c`: Remove `sdf_app_set_event_callback`, `sdf_app_set_audit_callback`, and `sdf_app_emit_event`/`sdf_app_emit_audit` from the security event path; update `sdf_app_on_event()` to depend solely on event router delivery
- `firmware/components/sdf_event_router/`: May need a new audit event type in `sdf_event_router_type_t` for audit events routed through the router
- `firmware/components/sdf_services/src/sdf_services.c`: `sdf_services_emit_security_event()` becomes the sole security event emitter
- `firmware/components/sdf_common/`: Event type enum may need an `SDF_EVENT_ROUTER_AUDIT` entry
- Documentation: `doc/sdf_sas.md` and `doc/user_manual.md` may need updates if event flow changes are visible
- Tests: `firmware/components/sdf_event_router/test/` and `firmware/components/sdf_app/test/` need updates for legacy callback removal