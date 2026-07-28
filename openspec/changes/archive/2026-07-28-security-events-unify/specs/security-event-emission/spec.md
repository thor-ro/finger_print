## ADDED Requirements

### Requirement: Security events emit exclusively through event router
The system SHALL emit all security events (match success, match failure, lockout entered, lockout cleared) exclusively through `sdf_event_router_emit()` rather than through a legacy callback mechanism.

#### Scenario: Match success emits single event
- **WHEN** fingerprint match succeeds for a valid user
- **THEN** exactly one `SDF_EVENT_ROUTER_BIOMETRIC_MATCH` event is emitted via the event router

#### Scenario: Match failure emits single event
- **WHEN** fingerprint match fails
- **THEN** exactly one `SDF_EVENT_ROUTER_BIOMETRIC_MATCH_FAILED` event is emitted via the event router

#### Scenario: Lockout entered emits single event
- **WHEN** failed attempt threshold is reached and lockout is entered
- **THEN** exactly one `SDF_EVENT_ROUTER_SECURITY_LOCKOUT` event with CRITICAL priority is emitted

#### Scenario: Lockout cleared emits single event
- **WHEN** lockout timer expires and lockout is cleared
- **THEN** exactly one `SDF_EVENT_ROUTER_SECURITY_LOCKOUT` event with NORMAL priority is emitted

### Requirement: No duplicate audit entries
The system SHALL produce exactly one audit log entry per security event, with no duplicate entries from the legacy callback path.

#### Scenario: Match failure produces single audit entry
- **WHEN** a match failure occurs
- **THEN** exactly one `BIOMETRIC_FAILED` audit event is logged

#### Scenario: Lockout entered produces single audit entry
- **WHEN** lockout is entered
- **THEN** exactly one `BIOMETRIC_LOCKOUT` audit event is logged

#### Scenario: Lockout cleared produces single audit entry
- **WHEN** lockout is cleared
- **THEN** exactly one `BIOMETRIC_LOCKOUT_CLEARED` audit event is logged

### Requirement: All event router subscribers receive security events
The system SHALL ensure that all subscribers registered with the event router receive security events, including `sdf_app_on_event()` for alarm mask and audit logic.

#### Scenario: sdf_app subscribes to BIOMETRIC_MATCH_FAILED
- **WHEN** `sdf_app_init()` runs
- **THEN** `sdf_app_on_event()` is subscribed to `SDF_EVENT_ROUTER_BIOMETRIC_MATCH_FAILED` at HIGH priority

#### Scenario: sdf_app subscribes to SECURITY_LOCKOUT
- **WHEN** `sdf_app_init()` runs
- **THEN** `sdf_app_on_event()` is subscribed to `SDF_EVENT_ROUTER_SECURITY_LOCKOUT` at CRITICAL priority

## REMOVED Requirements

### Requirement: Legacy callback-based security event emission
**Reason**: Replaced by unified event router path to eliminate duplicate audit entries and simplify maintenance.
**Migration**: Security events are now emitted exclusively via `sdf_event_router_emit()`. Subscribers use `sdf_event_router_subscribe()` instead of registering callbacks in `sdf_services_config_t`.