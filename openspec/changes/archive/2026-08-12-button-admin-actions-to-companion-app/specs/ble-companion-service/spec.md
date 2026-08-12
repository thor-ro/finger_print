## ADDED Requirements

### Requirement: Admin-Fingerprint-Gated Enroll-Admin Trigger
The Companion Service SHALL expose an authenticated action allowing an already-logged-in BLE client to request enrollment of a new administrator (`SDF_SERVICES_ADMIN_ACTION_ENROLL_ADMIN`). The request SHALL follow the same pending-admin-action pattern as Web Registration Authorization and Nuki Re-Pairing: the request enters a pending state, an Admin fingerprint must be scanned on the physical device within the pending-action timeout, and the result is routed back to the originating BLE connection. A valid logged-in companion session SHALL be required to submit the request, but SHALL NOT by itself be sufficient to authorize the action.

#### Scenario: Enroll-Admin request authorized
- **WHEN** an authenticated GATT client requests enrollment of a new admin
- **AND** an Admin finger is scanned successfully within the pending-action timeout
- **THEN** the system begins local fingerprint enrollment for the new user with admin permission
- **AND** the originating connection is notified that the request was authorized

#### Scenario: Enroll-Admin request denied
- **WHEN** an authenticated GATT client requests enrollment of a new admin
- **AND** a non-Admin finger is scanned, or the pending-action timeout elapses
- **THEN** the system denies the request
- **AND** the originating connection is notified of the denial

#### Scenario: Unauthenticated client cannot request Enroll-Admin
- **WHEN** a BLE client that has not completed Companion Service login attempts to request admin enrollment
- **THEN** the request is rejected without entering a pending state

### Requirement: Admin-Fingerprint-Gated Zigbee Join Trigger
The Companion Service SHALL expose an authenticated action allowing an already-logged-in BLE client to request a Zigbee join window (`SDF_SERVICES_ADMIN_ACTION_ZB_JOIN`). The request SHALL follow the same pending-admin-action pattern as Web Registration Authorization and Nuki Re-Pairing: the request enters a pending state, an Admin fingerprint must be scanned on the physical device within the pending-action timeout, and the result is routed back to the originating BLE connection. A valid logged-in companion session SHALL be required to submit the request, but SHALL NOT by itself be sufficient to authorize the action.

#### Scenario: Zigbee join request authorized
- **WHEN** an authenticated GATT client requests a Zigbee join window
- **AND** an Admin finger is scanned successfully within the pending-action timeout
- **THEN** the system opens the Zigbee join window
- **AND** the originating connection is notified that the request was authorized

#### Scenario: Zigbee join request denied
- **WHEN** an authenticated GATT client requests a Zigbee join window
- **AND** a non-Admin finger is scanned, or the pending-action timeout elapses
- **THEN** the system denies the request
- **AND** the originating connection is notified of the denial

#### Scenario: Unauthenticated client cannot request Zigbee join
- **WHEN** a BLE client that has not completed Companion Service login attempts to request a Zigbee join window
- **THEN** the request is rejected without entering a pending state

### Requirement: Pending BLE-Originated Admin Actions Always Resolve
Any BLE-originated pending admin action (Web Registration Authorization, Nuki Re-Pairing, Enroll-Admin, Zigbee Join) SHALL always resolve and notify its originating connection, even if the admin action completes with a result other than success while it is pending.

#### Scenario: Pending BLE-originated request resolves on non-success outcome
- **WHEN** an admin action completes with a result other than success while a BLE-originated pending request is outstanding
- **THEN** the system SHALL resolve the pending request as denied
- **AND** the GATT server SHALL notify the originating connection so no BLE client is left waiting indefinitely
