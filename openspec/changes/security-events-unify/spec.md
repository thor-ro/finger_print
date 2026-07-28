## Security Events Unification Specification

### Current State (Dual-Path)

Security events are emitted through two channels:
1. **Legacy callback**: `sdf_services_notify_security_event()` → calls `security_event_cb` + emits to event router
2. **Task direct**: `sdf_match_task_notify_security_event()` → emits to event router only

### Target State (Single-Path)

All security events emitted exclusively through event router:
- `SDF_EVENT_ROUTER_BIOMETRIC_MATCH` (HIGH)
- `SDF_EVENT_ROUTER_BIOMETRIC_MATCH_FAILED` (HIGH)
- `SDF_EVENT_ROUTER_SECURITY_LOCKOUT` (CRITICAL)

### Event Mapping

| Event Type | Priority | Emitted By | Previously Emitted By |
|-----------|----------|-----------|----------------------|
| `BIOMETRIC_MATCH` | HIGH | sdf_services_match.c | sdf_services.c (legacy) |
| `BIOMETRIC_MATCH_FAILED` | HIGH | sdf_services_match.c | sdf_services.c (legacy) |
| `SECURITY_LOCKOUT` | CRITICAL | sdf_services_match.c | sdf_services.c (legacy) |

### Changes Required

1. Remove `sdf_services_notify_security_event()` from `sdf_services.c`
2. Remove `sdf_match_task_notify_security_event()` from `sdf_services_match.c`
3. Ensure all match cycle code paths use direct event router emission
4. Remove `security_event_cb` from `sdf_services_config_t`
5. Update `sdf_app` to subscribe to event router security events
6. Remove `sdf_app_on_security_event()` callback registration