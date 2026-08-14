## ADDED Requirements

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
