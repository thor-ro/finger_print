## 1. Remove Legacy Callback Infrastructure

- [x] 1.1 Remove `sdf_app_set_event_callback()` and `sdf_app_set_audit_callback()` from `sdf_app.c`
- [x] 1.2 Remove `sdf_app_emit_event()` and make `sdf_app_emit_audit()` route through the event router
- [x] 1.3 Remove `SDF_APP_ZB_ALARM_SECURITY_PROTOCOL` macro if only used by legacy callback path (not removed — used by non-legacy BLE message handler)

## 2. Unify Security Event Emission Through Event Router

- [x] 2.1 Verify `sdf_services_emit_security_event()` is the only function emitting security events via `sdf_event_router_emit()`
- [x] 2.2 Add `SDF_EVENT_ROUTER_AUDIT` event type to `sdf_event_router_type_t` enum in `sdf_common.h`
- [x] 2.3 Add audit payload to the event router payload union in `sdf_event_router.h`
- [x] 2.4 Route `sdf_app_emit_audit()` calls for security events through `sdf_event_router_emit()` with `SDF_EVENT_ROUTER_AUDIT` type

## 3. Update sdf_app Event Handler

- [x] 3.1 Update `sdf_app_on_event()` to handle `SDF_EVENT_ROUTER_AUDIT` events for audit logging
- [x] 3.2 Remove any direct `sdf_app_emit_audit()` calls from the security event handling path in `sdf_app_on_event()` (now routed through event router)
- [x] 3.3 Verify `sdf_app_on_event()` subscribes to all security event types (BIOMETRIC_MATCH, BIOMETRIC_MATCH_FAILED, SECURITY_LOCKOUT, AUDIT)

## 4. Clean Up Legacy Code

- [x] 4.1 Remove unused callback type declarations (`sdf_event_cb`, `sdf_audit_cb`) from headers
- [x] 4.2 Remove callback-related fields from `sdf_app` internal state struct
- [x] 4.3 Update `sdf_app_init()` to not register legacy callbacks

## 5. Update Tests

- [ ] 5.1 Remove or update tests that rely on legacy callback mechanism in `sdf_app/test/`
- [ ] 5.2 Add test: verify no duplicate audit entries for single security event
- [ ] 5.3 Add test: verify legacy callback path is no longer invoked for security events
- [ ] 5.4 Update `test_sdf_event_router.c` to verify audit event delivery

## 6. Update Documentation

- [ ] 6.1 Update `doc/sdf_sas.md` §6 Runtime View to reflect unified event flow
- [ ] 6.2 Update `doc/software-architecture.md` if it shows legacy callback paths
- [ ] 6.3 Update `AGENTS.md` Component Structure if callback removal changes component boundaries