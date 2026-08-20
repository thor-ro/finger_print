# Security Event Unification

## Purpose

Specifies the unification of security event routing and elimination of duplicate audit logging.

## Requirements

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

### Requirement: Lockout subscriber accepts both lockout emissions
The subscriber that consumes `SDF_EVENT_ROUTER_SECURITY_LOCKOUT` SHALL register with a `min_prio` that admits both the CRITICAL "lockout entered" emission and the NORMAL "lockout cleared" emission. A subscription whose priority filter excludes either emission SHALL be treated as a defect, because the lockout alarm state and the lockout audit trail both depend on receiving the pair.

#### Scenario: Lockout entered is delivered
- **WHEN** lockout is entered and `SDF_EVENT_ROUTER_SECURITY_LOCKOUT` is emitted with CRITICAL priority
- **THEN** the subscriber callback is invoked and the biometric lockout alarm state is set

#### Scenario: Lockout cleared is delivered
- **WHEN** the lockout timer expires and `SDF_EVENT_ROUTER_SECURITY_LOCKOUT` is emitted with NORMAL priority
- **THEN** the subscriber callback is invoked, the biometric lockout alarm state is cleared, and exactly one `BIOMETRIC_LOCKOUT_CLEARED` audit event is logged

#### Scenario: Lockout alarm does not latch across a lockout cycle
- **WHEN** a device enters lockout and the lockout subsequently expires
- **THEN** the reported biometric lockout alarm state returns to cleared without requiring a reboot