## MODIFIED Requirements

### Requirement: BLE GATT Authentication
The system SHALL expose an Authentication characteristic supporting a two-step challenge-response LOGIN and a single-step REGISTER. Until a LOGIN challenge is successfully verified on this characteristic, all other restricted characteristics (Config, Enrollment, OTA) SHALL return insufficient authentication errors.

REGISTER SHALL accept a username and a client-computed password hash, as today. On successful registration, the system SHALL generate a random per-user salt and derive a stretched credential from the received hash using a key-derivation function; the system SHALL persist only the salt and stretched credential, never the raw received hash.

LOGIN SHALL proceed as: (1) the client submits `LOGIN_INIT` with a username; (2) the system replies with that user's salt, the key-derivation iteration count, and a freshly generated single-use nonce; (3) the client submits `LOGIN_VERIFY` with a response value computed by the client; (4) the system compares the submitted response against a value it computes from the stored stretched credential and the same nonce, and authenticates the connection only on a match. The nonce SHALL be invalidated after being consumed by one `LOGIN_VERIFY` attempt (successful or not) and SHALL never be reused.

For a username with no matching stored account, `LOGIN_INIT` SHALL respond with a deterministic salt and nonce indistinguishable in structure and timing from a real account's response, so that an observer cannot determine whether a given username is registered.

#### Scenario: Unauthorized access blocked
- **WHEN** an unauthenticated client attempts to write to the Config characteristic
- **THEN** system returns ESP_GATT_AUTH_FAIL

#### Scenario: LOGIN challenge issued for a registered user
- **WHEN** a client submits `LOGIN_INIT` with a username that has a stored account
- **THEN** system replies with that account's stored salt, the key-derivation iteration count, and a newly generated single-use nonce

#### Scenario: LOGIN challenge issued for an unknown user is indistinguishable
- **WHEN** a client submits `LOGIN_INIT` with a username that has no stored account
- **THEN** system replies with a deterministic salt and a newly generated nonce in the same shape as a registered-user response
- **AND** no subsequent `LOGIN_VERIFY` response can succeed for that username

#### Scenario: Successful authentication
- **WHEN** a client submits `LOGIN_VERIFY` with a response value that matches what the system computes from the stored stretched credential and the outstanding nonce
- **THEN** system transitions the BLE connection to authenticated state
- **AND** subsequent writes to the Config characteristic succeed

#### Scenario: Failed authentication
- **WHEN** a client submits `LOGIN_VERIFY` with a response value that does not match what the system computes from the stored stretched credential and the outstanding nonce
- **THEN** system does not transition the BLE connection to authenticated state
- **AND** the nonce is invalidated and cannot be reused

#### Scenario: Nonce cannot be replayed
- **WHEN** a client submits a second `LOGIN_VERIFY` reusing a nonce that was already consumed by a prior `LOGIN_VERIFY` on the same connection
- **THEN** system rejects the attempt without authenticating the connection

#### Scenario: Registered credential is never stored as the raw received hash
- **WHEN** a client successfully registers a new account
- **THEN** system persists a per-user salt and a key-derivation-stretched credential
- **AND** system does not persist the raw hash it received over the wire

### Requirement: Failed BLE Login Lockout With Bond Eviction
The Companion Service SHALL track consecutive failed `LOGIN_VERIFY` attempts per bonded identity in memory, tied to its bond-tracking state so the count survives disconnect and reconnect within device uptime. This counter is intentionally not persisted across reboot. `LOGIN_INIT` requests SHALL NOT count toward this counter. Upon a bonded identity reaching the configured failed-attempt threshold (a compile-time constant, default 3), the system SHALL remove that identity's bond record and allow-list entry and SHALL terminate its live connection immediately.

#### Scenario: Failed attempt increments counter
- **WHEN** a bonded, connected device submits a `LOGIN_VERIFY` response that does not match the expected value for the outstanding nonce
- **THEN** the system increments that identity's failed-login counter
- **AND** the system does not disconnect the device

#### Scenario: Successful login resets counter
- **WHEN** a bonded, connected device submits a `LOGIN_VERIFY` response that matches the expected value for the outstanding nonce
- **THEN** the system resets that identity's failed-login counter to zero

#### Scenario: Requesting a challenge does not count as an attempt
- **WHEN** a bonded, connected device submits `LOGIN_INIT`
- **THEN** the system does not change that identity's failed-login counter

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
