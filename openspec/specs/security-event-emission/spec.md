# security-event-emission Specification

## Purpose
TBD - created by archiving change security-events-unify. Update Purpose after archive.
## Requirements
### Requirement: Security events emit exclusively through event router
The system SHALL emit all security events (match success, match failure, lockout entered, lockout cleared) exclusively through `sdf_event_router_emit()` rather than through a legacy callback mechanism. **The legacy callback registration functions (`sdf_app_set_event_callback`, `sdf_app_set_audit_callback`) SHALL NOT be used for any security event type.**

#### Scenario: Match success emits single event via event router
- **WHEN** fingerprint match succeeds for a valid user
- **THEN** exactly one `SDF_EVENT_ROUTER_BIOMETRIC_MATCH` event is emitted via `sdf_event_router_emit()`
- **AND** no legacy callback is invoked for this event

#### Scenario: Match failure emits single event via event router
- **WHEN** fingerprint match fails
- **THEN** exactly one `SDF_EVENT_ROUTER_BIOMETRIC_MATCH_FAILED` event is emitted via `sdf_event_router_emit()`
- **AND** no legacy callback is invoked for this event

#### Scenario: Lockout entered emits single event via event router
- **WHEN** failed attempt threshold is reached and lockout is entered
- **THEN** exactly one `SDF_EVENT_ROUTER_SECURITY_LOCKOUT` event with CRITICAL priority is emitted via `sdf_event_router_emit()`
- **AND** no legacy callback is invoked for this event

#### Scenario: Lockout cleared emits single event via event router
- **WHEN** lockout timer expires and lockout is cleared
- **THEN** exactly one `SDF_EVENT_ROUTER_SECURITY_LOCKOUT` event with NORMAL priority is emitted via `sdf_event_router_emit()`
- **AND** no legacy callback is invoked for this event

### Requirement: No duplicate audit entries
The system SHALL produce exactly one audit log entry per security event. **The legacy callback path that previously produced duplicate entries SHALL be removed entirely.**

#### Scenario: Match failure produces single audit entry
- **WHEN** a match failure occurs
- **THEN** exactly one `BIOMETRIC_FAILED` audit event is logged

#### Scenario: Lockout entered produces single audit entry
- **WHEN** lockout is entered
- **THEN** exactly one `BIOMETRIC_LOCKOUT` audit event is logged

#### Scenario: Lockout cleared produces single audit entry
- **WHEN** lockout is cleared
- **THEN** exactly one `BIOMETRIC_LOCKOUT_CLEARED` audit event is logged

### Requirement: Audit events route through event router
The system SHALL emit all audit events for security operations through `sdf_event_router_emit()` with `SDF_EVENT_ROUTER_AUDIT` type rather than via direct `sdf_app_emit_audit()` callback invocation. `sdf_app_on_event()` SHALL handle `SDF_EVENT_ROUTER_AUDIT` events for audit logging and shall subscribe to them at NORMAL priority.

#### Scenario: Audit event emitted for lockout entered
- **WHEN** lockout is entered and `sdf_app_emit_audit(SDF_AUDIT_BIOMETRIC_LOCKOUT, ...)` is called
- **THEN** exactly one `SDF_EVENT_ROUTER_AUDIT` event with `SDF_AUDIT_BIOMETRIC_LOCKOUT` type is emitted via `sdf_event_router_emit()`

#### Scenario: sdf_app subscribes to AUDIT events
- **WHEN** `sdf_app_init()` runs
- **THEN** `sdf_app_on_event()` is subscribed to `SDF_EVENT_ROUTER_AUDIT` at NORMAL priority

### Requirement: All event router subscribers receive security events
The system SHALL ensure that all subscribers registered with the event router receive security events, including `sdf_app_on_event()` for alarm mask and audit logic.

