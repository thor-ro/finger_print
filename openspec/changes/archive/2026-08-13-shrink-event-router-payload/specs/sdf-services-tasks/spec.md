## MODIFIED Requirements

### Requirement: Web Registration Authorization
The `sdf_admin_task` and `sdf_services` SHALL support an "Admin Auth" state where `sdf_admin_task` waits for an Admin fingerprint scan to authorize a pending Web Registration request over BLE GATT. The authorization result SHALL be routed back to the GATT server for the originating connection. The registration decision (what user record to persist, and what reply to send) SHALL be computed by a pure `sdf_services` function, independent of the GATT transport that carries the request and reply. Raw web-registration credential material (username and password hash) SHALL NOT be carried in an event-router event payload or copied into any event-router queue at any point in this flow; it SHALL be written directly into `sdf_services`' owned pending-request state, and any component that needs it (including the routing of the authorization result back to the GATT server) SHALL read it back from that owned state rather than from an event payload.

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

#### Scenario: Registration request credential material bypasses the event router
- **WHEN** the GATT server receives a Web Registration request containing a username and password hash
- **THEN** the username and password hash are written directly into `sdf_services`' owned pending-request state
- **AND** no event carrying the raw username or password hash is emitted or queued

#### Scenario: Registration result routing reads from owned state, not from an event payload
- **WHEN** `sdf_admin_task` resolves a pending Web Registration request (authorized or denied)
- **THEN** the username and permission used to route the result back to the originating GATT connection are read from `sdf_services`' owned pending-request state
- **AND** the event that signals the outcome does not itself carry the username or permission as a payload field
