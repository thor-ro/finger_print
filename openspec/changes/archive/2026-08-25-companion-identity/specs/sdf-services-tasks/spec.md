## MODIFIED Requirements

### Requirement: Web Registration Authorization
The `sdf_admin_task` and `sdf_services` SHALL support an "Admin Auth" state where `sdf_admin_task` waits for an Admin fingerprint scan to authorize a pending Web Registration request over BLE GATT. The authorization result SHALL be routed back to the GATT server for the originating connection. The registration decision (what user record to persist, and what reply to send) SHALL be computed by a pure `sdf_services` function, independent of the GATT transport that carries the request and reply. Raw web-registration credential material (name and password hash) SHALL NOT be carried in an event-router event payload or copied into any event-router queue at any point in this flow; it SHALL be written directly into `sdf_services`' owned pending-request state, and any component that needs it (including the routing of the authorization result back to the GATT server) SHALL read it back from that owned state rather than from an event payload.

When an Admin fingerprint scan authorizes a pending Web Registration request, the system SHALL capture the fingerprint user id of the matched admin into the same owned pending-request state, and the registration decision SHALL bind the persisted credential to that user id. The captured user id SHALL be subject to the same rule as the credential material: it SHALL NOT be carried in an event-router event payload.

The registration decision SHALL replace the credential of a user that already holds an account rather than creating a second account for that user, and SHALL refuse to persist a credential for which no authorizing admin user id was captured.

#### Scenario: Web Registration Authorized
- **WHEN** GATT server requests web registration authorization
- **AND** Admin finger is scanned successfully
- **THEN** system authorizes the registration
- **AND** GATT server saves credentials to NVS bound to the matched admin's user id
- **AND** GATT server marks the originating connection authenticated only after the credentials are saved

#### Scenario: Web Registration Denied
- **WHEN** GATT server requests web registration authorization
- **AND** non-Admin finger is scanned (or timeout occurs)
- **THEN** system denies the registration

#### Scenario: Pending registration always resolves
- **WHEN** an admin action completes with a result other than success while a Web Registration Authorization request is pending
- **THEN** the system SHALL resolve the pending request as denied
- **AND** the GATT server SHALL be notified so no BLE client is left waiting indefinitely

#### Scenario: Registration request credential material bypasses the event router
- **WHEN** the GATT server receives a Web Registration request containing a name and password hash
- **THEN** the name and password hash are written directly into `sdf_services`' owned pending-request state
- **AND** no event carrying the raw name or password hash is emitted or queued

#### Scenario: Authorizing admin identity bypasses the event router
- **WHEN** an Admin fingerprint scan authorizes a pending Web Registration request
- **THEN** the matched admin's user id is written into `sdf_services`' owned pending-request state
- **AND** no event carrying that user id is emitted or queued

#### Scenario: Registration result routing reads from owned state, not from an event payload
- **WHEN** `sdf_admin_task` resolves a pending Web Registration request (authorized or denied)
- **THEN** the name and bound user id used to route the result back to the originating GATT connection are read from `sdf_services`' owned pending-request state
- **AND** the event that signals the outcome does not itself carry the name or bound user id as a payload field

#### Scenario: Registration by an admin who already holds an account
- **WHEN** the registration decision runs for an admin whose user id already has a stored credential
- **THEN** the decision replaces that credential rather than allocating a second account
- **AND** the previous salt and stretched credential are not retained

#### Scenario: Registration without a captured authorizer is refused
- **WHEN** the registration decision runs with no authorizing admin user id in the pending-request state
- **THEN** the decision refuses the registration
- **AND** no credential is persisted

### Requirement: Web Login Verification
`sdf_services` SHALL expose a pure function that decides whether submitted BLE companion login credentials are valid, given a previously looked-up stored user record and a submitted password hash. The comparison SHALL use a constant-time algorithm with respect to the submitted hash value.

The permission that governs the resulting session SHALL NOT be taken from the stored account record. It SHALL be resolved from the enrolled-user record of the fingerprint user the account is bound to, at the time each authorization decision is made, so that a demotion or deletion of that user takes effect without the account being modified.

#### Scenario: Valid login credentials
- **WHEN** a submitted password hash matches the stored user's password hash exactly
- **THEN** the function reports the login as valid

#### Scenario: Invalid login credentials
- **WHEN** a submitted password hash does not match the stored user's password hash, or has an unexpected length
- **THEN** the function reports the login as invalid
- **AND** the comparison does not use an early-exit algorithm that could leak which byte first differed

#### Scenario: Session permission resolved from the bound user
- **WHEN** an authorization decision is made for an authenticated session
- **THEN** the permission used is read from the enrolled-user record of the bound fingerprint user
- **AND** it is not read from the stored account record

#### Scenario: Login refused when the bound user is no longer an admin
- **WHEN** a login is attempted against an account whose bound user's permission is no longer admin
- **THEN** the session is not granted admin authority
