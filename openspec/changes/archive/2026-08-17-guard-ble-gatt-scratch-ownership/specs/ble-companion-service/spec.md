# Spec Delta: ble-companion-service

## ADDED Requirements

### Requirement: Single-Owner GATT Write Staging

The Companion Service stages the payload of an inbound GATT write in shared storage so the payload can be handed to an application callback after the service lock is released. That storage SHALL be owned by at most one caller at a time, and ownership SHALL be enforced by the service rather than assumed from call-site discipline.

The Companion Service SHALL refuse a staging request when the storage is already held, and when the requesting task is not the task that owns GATT write staging. A refusal SHALL NOT return storage that another caller is using, SHALL be recorded in a diagnostic counter, and SHALL be logged at error level identifying it as a contract violation rather than a client error.

A refused staging request SHALL fail only the GATT operation that triggered it, returning an ATT error to the client and leaving the device running, advertising, and able to serve subsequent requests. The Companion Service SHALL NOT abort or restart on a refusal.

The Companion Service SHALL release staged storage before returning from the GATT operation that acquired it, on every exit path — including paths that reject the write for malformed length, unknown command, failed authentication, or any other error — so that a rejected write cannot leave staging permanently held.

Notification payloads emitted outside the GATT request path SHALL NOT use GATT write staging storage. Emitting a notification SHALL be possible concurrently with an in-flight GATT write without the two sharing a buffer.

#### Scenario: Staging requested from a non-owning task

- **WHEN** a caller running on a task other than the one that owns GATT write staging requests staging storage
- **THEN** the request is refused, the violation counter increments, and an error is logged
- **AND** any staged payload belonging to an in-flight GATT write is left intact
- **AND** the device continues operating normally

#### Scenario: Staging requested while already held

- **WHEN** a staging request arrives while a previous acquisition has not yet been released
- **THEN** the request is refused rather than returning the in-use storage
- **AND** the GATT operation that made the second request fails with an ATT error

#### Scenario: Sequential writes across characteristics

- **WHEN** a client writes the Auth, Config, Enroll, and OTA characteristics in sequence
- **THEN** each write acquires and releases staging in turn and completes successfully
- **AND** no write observes a payload belonging to another characteristic or another connection

#### Scenario: Write rejected before completion

- **WHEN** a GATT write acquires staging and is then rejected for a malformed length, an unrecognized command, or a failed authentication check
- **THEN** staging is released before the ATT error is returned
- **AND** the next GATT write on any characteristic acquires staging successfully

#### Scenario: Notification concurrent with an in-flight write

- **WHEN** a notification is emitted from the event-router or OTA task while a GATT write is being staged on the host task
- **THEN** the notification payload is carried in storage independent of GATT write staging
- **AND** neither the notification payload nor the staged write payload is corrupted

## MODIFIED Requirements

### Requirement: BLE GATT Authentication
The system SHALL expose an Authentication characteristic supporting a two-step challenge-response LOGIN and a single-step REGISTER. Until a LOGIN challenge is successfully verified on this characteristic, all other restricted characteristics (Config, Enrollment, OTA) SHALL return insufficient authentication errors.

REGISTER SHALL accept a username and a client-computed password hash, as today. On successful registration, the system SHALL generate a random per-user salt and derive a stretched credential from the received hash using a key-derivation function; the system SHALL persist only the salt and stretched credential, never the raw received hash.

LOGIN SHALL proceed as: (1) the client submits `LOGIN_INIT` with a username; (2) the system replies with that user's salt, the key-derivation iteration count, and a freshly generated single-use nonce; (3) the client submits `LOGIN_VERIFY` with a response value computed by the client; (4) the system compares the submitted response against a value it computes from the stored stretched credential and the same nonce, and authenticates the connection only on a match. The nonce SHALL be invalidated after being consumed by one `LOGIN_VERIFY` attempt (successful or not) and SHALL never be reused.

For a username with no matching stored account, `LOGIN_INIT` SHALL respond with a deterministic salt and nonce indistinguishable in structure and timing from a real account's response, so that an observer cannot determine whether a given username is registered.

Every command accepted on the Authentication characteristic SHALL have a defined length, and the system SHALL reject a write whose length does not match the command it carries. No command SHALL be accepted on the basis of an unbounded or characteristic-wide length allowance alone.

A write to the Authentication characteristic SHALL be bounded by the largest well-formed command the characteristic accepts, which is REGISTER at command byte, username length byte, maximum username, and password hash. The system SHALL reject a longer write with an invalid-length error before copying it and before dispatching on its command byte, so that an oversized write is never staged or interpreted.

LOGOUT carries no operands and SHALL be exactly one byte; a LOGOUT write of any other length SHALL be rejected with an invalid-length error and SHALL NOT log the connection out. LOGIN_INIT, LOGIN_VERIFY and REGISTER SHALL each be accepted only at the exact length implied by their own encoding.

The Authentication characteristic's per-command wire format SHALL be documented, and that documentation SHALL match the lengths the system enforces.

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

#### Scenario: Oversized authentication write rejected before dispatch
- **WHEN** a client writes more bytes to the Authentication characteristic than the largest well-formed command allows
- **THEN** system rejects the write with an invalid-length error
- **AND** the write is not copied into staging and its command byte is not acted on
- **AND** the connection's authentication state is unchanged

#### Scenario: LOGOUT accepted only at its exact length
- **WHEN** an authenticated client writes a single-byte LOGOUT command
- **THEN** system returns the connection to an unauthenticated state
- **AND** subsequent writes to the Config characteristic return insufficient authentication errors

#### Scenario: Padded LOGOUT rejected
- **WHEN** a client writes a LOGOUT command carrying any trailing bytes
- **THEN** system rejects the write with an invalid-length error rather than performing the logout
- **AND** the connection remains in the authentication state it held before the write
- **AND** a subsequent single-byte LOGOUT on the same connection succeeds

#### Scenario: Documented wire format matches enforcement
- **WHEN** the Authentication characteristic's documented per-command lengths are compared against the lengths the system accepts
- **THEN** they agree for every command, including commands that carry no operands
