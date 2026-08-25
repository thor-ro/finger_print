## Purpose

Defines how the BLE Companion Service records that a peer has been granted trust, so that allow-list membership is an explicit act that was performed rather than a property inferred from the presence of cryptographic keys.

## ADDED Requirements

### Requirement: Admission Is Recorded Explicitly
The system SHALL persist an admission record identifying each peer that has been granted allow-list trust. An admission record SHALL be written only at a point where trust is deliberately granted, and SHALL NOT be created as a side effect of pairing, bonding, connecting, or authenticating.

The system SHALL support at least as many admission records as it supports persisted bonds.

#### Scenario: Admission written at setup completion
- **WHEN** the setup phase ends via an explicit completion request from a connected client
- **THEN** the system persists an admission record for that client's bonded identity

#### Scenario: Admission written on pairing-window admit
- **WHEN** a device completes bonding during an open admin-fingerprint-gated pairing window and is admitted
- **THEN** the system persists an admission record for that device's bonded identity

#### Scenario: Bonding alone creates no admission
- **WHEN** a peer pairs and bonds with the device without a corresponding admission event
- **THEN** no admission record is persisted for that peer

### Requirement: Allow List Is The Intersection Of Admission And Bonding
The system SHALL populate its allow list at startup from the intersection of persisted admission records and the persisted bond store. A peer SHALL be allow-listed only if it appears in both. The system SHALL NOT infer allow-list membership from presence in the bond store alone.

#### Scenario: Abandoned setup-phase bond is not trusted
- **WHEN** a client bonds during the setup phase, never completes setup, and the device later reboots
- **THEN** that client is not present in the allow list
- **AND** it cannot connect once the device is advertising with allow-list filtering

#### Scenario: Admitted and bonded peer is trusted across reboot
- **WHEN** a peer holds both an admission record and a persisted bond
- **AND** the device reboots
- **THEN** that peer is present in the allow list and reconnects without re-pairing

#### Scenario: Admission without keys grants nothing
- **WHEN** an admission record exists for a peer whose bond is no longer in the bond store
- **THEN** that peer is not added to the allow list

#### Scenario: Runtime admission failure does not become trust at reboot
- **WHEN** a peer bonds but the system fails to record its admission
- **AND** the device reboots
- **THEN** the peer is not allow-listed, matching the trust it was granted at runtime

### Requirement: Revoking Trust Clears Both Records
When the system revokes a peer's trust, it SHALL remove both that peer's bond record and its admission record. Removing only one SHALL NOT be sufficient, so that a revoked peer which later re-bonds is not silently re-admitted by a surviving admission record.

#### Scenario: Failed-login eviction clears admission
- **WHEN** a bonded identity reaches the failed-login threshold and is evicted
- **THEN** the system removes its bond record, its allow-list entry, and its admission record

#### Scenario: Re-bonding after eviction does not restore trust
- **WHEN** an evicted peer bonds again during a subsequent pairing window without being admitted
- **AND** the device reboots
- **THEN** that peer is not present in the allow list

#### Scenario: Factory reset clears all admission records
- **WHEN** a factory reset completes
- **THEN** no admission records remain persisted
