## ADDED Requirements

### Requirement: All security events emit through event router only
The system SHALL emit all security events exclusively through `sdf_event_router_emit()`. The legacy callback mechanism (`sdf_app_set_event_callback`, `sdf_app_set_audit_callback`) SHALL NOT be used for any security event type.

#### Scenario: Match success emits via event router only
- **WHEN** fingerprint match succeeds for a valid user
- **THEN** exactly one `SDF_EVENT_ROUTER_BIOMETRIC_MATCH` event is emitted via `sdf_event_router_emit()`
- **AND** no legacy callback is invoked for this event

#### Scenario: Match failure emits via event router only
- **WHEN** fingerprint match fails
- **THEN** exactly one `SDF_EVENT_ROUTER_BIOMETRIC_MATCH_FAILED` event is emitted via `sdf_event_router_emit()`
- **AND** no legacy callback is invoked for this event

#### Scenario: Lockout entered emits via event router only
- **WHEN** failed attempt threshold is reached and lockout is entered
- **THEN** exactly one `SDF_EVENT_ROUTER_SECURITY_LOCKOUT` event with CRITICAL priority is emitted via `sdf_event_router_emit()`
- **AND** no legacy callback is invoked for this event

#### Scenario: Lockout cleared emits via event router only
- **WHEN** lockout timer expires and lockout is cleared
- **THEN** exactly one `SDF_EVENT_ROUTER_SECURITY_LOCKOUT` event with NORMAL priority is emitted via `sdf_event_router_emit()`
- **AND** no legacy callback is invoked for this event

### Requirement: No duplicate audit entries
The system SHALL produce exactly one audit log entry per security event. The legacy callback path that previously produced duplicate audit entries SHALL be removed.

#### Scenario: Match failure produces single audit entry
- **WHEN** a match failure occurs
- **THEN** exactly one `BIOMETRIC_FAILED` audit event is logged

#### Scenario: Lockout entered produces single audit entry
- **WHEN** lockout is entered
- **THEN** exactly one `BIOMETRIC_LOCKOUT` audit event is logged

#### Scenario: Lockout cleared produces single audit entry
- **WHEN** lockout is cleared
- **THEN** exactly one `BIOMETRIC_LOCKOUT_CLEARED` audit event is logged