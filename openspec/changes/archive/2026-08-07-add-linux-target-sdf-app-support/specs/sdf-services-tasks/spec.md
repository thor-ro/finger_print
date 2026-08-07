## MODIFIED Requirements

### Requirement: Web Registration Authorization
The `sdf_admin_task` and `sdf_services` SHALL support an "Admin Auth" state where `sdf_admin_task` waits for an Admin fingerprint scan to authorize a pending Web Registration request over BLE GATT. The authorization result SHALL be routed back to the GATT server for the originating connection. The registration decision (what user record to persist, and what reply to send) SHALL be computed by a pure `sdf_services` function, independent of the GATT transport that carries the request and reply.

#### Scenario: Web Registration Authorized
- **WHEN** GATT server requests web registration authorization
- **AND** Admin finger is scanned successfully
- **THEN** system authorizes the registration
- **AND** GATT server saves credentials to NVS
- **AND** GATT server marks the originating connection authenticated only after the credentials are saved

#### Scenario: Web Registration Denied
- **WHEN** GATT server requests web registration authorization
- **AND** non-Admin finger is scanned (or timeout occurs)
- **THEN** system denies the registration

#### Scenario: Pending registration always resolves
- **WHEN** an admin action completes with a result other than success while a Web Registration Authorization request is pending
- **THEN** the system SHALL resolve the pending request as denied
- **AND** the GATT server SHALL be notified so no BLE client is left waiting indefinitely

## ADDED Requirements

### Requirement: Web Login Verification
`sdf_services` SHALL expose a pure function that decides whether submitted BLE companion login credentials are valid, given a previously looked-up stored user record and a submitted password hash. The comparison SHALL use a constant-time algorithm with respect to the submitted hash value.

#### Scenario: Valid login credentials
- **WHEN** a submitted password hash matches the stored user's password hash exactly
- **THEN** the function reports the login as valid

#### Scenario: Invalid login credentials
- **WHEN** a submitted password hash does not match the stored user's password hash, or has an unexpected length
- **THEN** the function reports the login as invalid
- **AND** the comparison does not use an early-exit algorithm that could leak which byte first differed
