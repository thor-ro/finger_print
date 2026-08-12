## ADDED Requirements

### Requirement: Sparse, Allow-List-Filtered Advertising
The Companion Service SHALL advertise using a sparse duty cycle and an accept/allow-list filter policy by default, such that only bonded identities already present on the allow list can complete a connection. This replaces the prior behavior of continuous, unfiltered, undirected advertising.

#### Scenario: Unknown device cannot connect
- **WHEN** a BLE device not present on the allow list attempts to connect while the Companion Service is in its default advertising mode
- **THEN** the connection attempt SHALL NOT succeed

#### Scenario: Allow-listed device reconnects normally
- **WHEN** a BLE device already present on the allow list attempts to connect while the Companion Service is in its default advertising mode
- **THEN** the connection SHALL be accepted and proceed through the existing bonded/encrypted link flow

### Requirement: Admin-Fingerprint-Gated Device Pairing Window
The Companion Service SHALL expose an admin-fingerprint-gated action, triggered by the button task's Double-Press gesture, that opens a single-shot, time-boxed pairing window during which advertising is unfiltered. The window duration SHALL be a compile-time constant, default 60 seconds. The window SHALL close immediately upon the first successful bond completed during it, and that bonded identity SHALL be added to the allow list without any further authorization step. Stray or incomplete connection attempts (connections that do not complete bonding) during the window SHALL be ignored and SHALL NOT close or extend the window.

#### Scenario: Pairing window opened after fingerprint approval
- **WHEN** Double-Press occurs on the physical button
- **AND** an Admin finger is scanned successfully within the pending-action timeout
- **THEN** the system opens unfiltered advertising for up to the configured window duration

#### Scenario: First bond closes the window and grants trust
- **WHEN** a device completes bonding during an open pairing window
- **THEN** the system adds that device's bonded identity to the allow list
- **AND** the system immediately closes the pairing window and returns to sparse, allow-list-filtered advertising

#### Scenario: Incomplete connection does not consume the window
- **WHEN** a device connects during an open pairing window but does not complete bonding
- **THEN** the pairing window SHALL remain open
- **AND** that connection attempt SHALL NOT be added to the allow list

#### Scenario: Window closes on timeout with no bond
- **WHEN** no device completes bonding before the configured window duration elapses
- **THEN** the system closes the pairing window and returns to sparse, allow-list-filtered advertising

#### Scenario: Pairing window denied
- **WHEN** Double-Press occurs on the physical button
- **AND** a non-Admin finger is scanned, or the pending-action timeout elapses
- **THEN** the system denies the request and does not open the pairing window

### Requirement: Failed BLE Login Lockout With Bond Eviction
The Companion Service SHALL track consecutive failed LOGIN attempts per bonded identity in memory, tied to its bond-tracking state so the count survives disconnect and reconnect within device uptime. This counter is intentionally not persisted across reboot. Upon a bonded identity reaching the configured failed-attempt threshold (a compile-time constant, default 3), the system SHALL remove that identity's bond record and allow-list entry and SHALL terminate its live connection immediately.

#### Scenario: Failed attempt increments counter
- **WHEN** a bonded, connected device submits a LOGIN whose password hash does not match the stored user's hash
- **THEN** the system increments that identity's failed-login counter
- **AND** the system does not disconnect the device

#### Scenario: Successful login resets counter
- **WHEN** a bonded, connected device submits a LOGIN whose password hash matches the stored user's hash
- **THEN** the system resets that identity's failed-login counter to zero

#### Scenario: Threshold reached evicts the device
- **WHEN** a bonded identity's failed-login counter reaches the configured threshold
- **THEN** the system removes that identity's bond record and allow-list entry
- **AND** the system terminates the live connection immediately

#### Scenario: Evicted device cannot reconnect without re-pairing
- **WHEN** a device whose bond was removed due to lockout attempts to connect again
- **THEN** the connection attempt SHALL NOT succeed, since the identity is no longer on the allow list
- **AND** the device can only regain access via the Admin-Fingerprint-Gated Device Pairing Window

#### Scenario: Reconnecting does not reset the counter
- **WHEN** a bonded device disconnects and reconnects before its failed-login counter reaches the threshold
- **THEN** its failed-login counter SHALL retain its prior value rather than resetting to zero
