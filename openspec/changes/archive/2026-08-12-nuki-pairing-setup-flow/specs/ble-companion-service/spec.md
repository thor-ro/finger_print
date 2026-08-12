## ADDED Requirements

### Requirement: Admin-Fingerprint-Gated Nuki Re-Pairing Trigger
The Companion Service SHALL expose an authenticated action allowing an already-logged-in BLE client to request Nuki re-pairing. This action SHALL only be reachable after initial setup is complete (i.e. Nuki credentials are already persisted). The request SHALL be authorized using the same admin-fingerprint pending-action pattern as Web Registration Authorization: the request enters a pending state, an Admin fingerprint must be scanned on the physical device within the pending-action timeout, and the result is routed back to the originating BLE connection. Standard BLE Companion Service connection authentication (a valid logged-in session) SHALL be required to submit the request, but SHALL NOT by itself be sufficient to authorize the pairing — the on-device Admin fingerprint scan is always required in addition.

#### Scenario: Nuki re-pair authorized
- **WHEN** an authenticated GATT client requests Nuki re-pairing
- **AND** an Admin finger is scanned successfully within the pending-action timeout
- **THEN** the system starts Nuki BLE pairing
- **AND** the originating connection is notified that pairing has started

#### Scenario: Nuki re-pair denied
- **WHEN** an authenticated GATT client requests Nuki re-pairing
- **AND** a non-Admin finger is scanned, or the pending-action timeout elapses
- **THEN** the system denies the request
- **AND** the originating connection is notified of the denial

#### Scenario: Pending re-pair request always resolves
- **WHEN** an admin action completes with a result other than success while a Nuki re-pairing request is pending
- **THEN** the system SHALL resolve the pending request as denied
- **AND** the GATT server SHALL notify the originating connection so no BLE client is left waiting indefinitely

#### Scenario: Unauthenticated client cannot request Nuki re-pairing
- **WHEN** a BLE client that has not completed Companion Service login attempts to request Nuki re-pairing
- **THEN** the request is rejected without entering a pending state

#### Scenario: Re-pairing trigger unreachable before setup is complete
- **WHEN** an authenticated GATT client requests Nuki re-pairing
- **AND** setup is not yet complete (no Nuki credentials persisted)
- **THEN** the request is rejected, since initial pairing during setup is reached via the physical button flow, not this trigger
