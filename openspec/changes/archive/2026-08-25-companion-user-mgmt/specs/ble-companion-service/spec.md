## MODIFIED Requirements

### Requirement: BLE GATT Authentication
The system SHALL expose an Authentication characteristic supporting a two-step challenge-response LOGIN and a single-step REGISTER. Until a LOGIN challenge is successfully verified on this characteristic, all other restricted characteristics (Config, Enrollment, OTA) SHALL return insufficient authentication errors, with the single exception stated in "Setup-Phase Admission To The Enrollment Characteristic" - which exists because the first Admin must be enrolled before any account exists to log in with.

REGISTER SHALL accept a name and a client-computed password hash. On successful registration, the system SHALL generate a random per-user salt and derive a stretched credential from the received hash using a key-derivation function; the system SHALL persist only the salt and stretched credential, never the raw received hash. The persisted credential SHALL be bound to the fingerprint user whose scan authorized the registration, and SHALL replace that user's existing credential if they already hold one. The submitted name SHALL become that user's name, subject to the uniqueness rule in the `companion-identity` capability.

An authenticated connection SHALL record the fingerprint user id of the account that authenticated it. Whenever the system decides whether that connection may perform an action requiring admin permission, it SHALL read the bound user's current permission rather than any value stored on the account. A connection whose bound user has been demoted or deleted SHALL therefore lose admin authority without the account needing to be modified, and without any cascade over stored records.

LOGIN SHALL proceed as: (1) the client submits `LOGIN_INIT` with a name; (2) the system replies with that user's salt, the key-derivation iteration count, and a freshly generated single-use nonce; (3) the client submits `LOGIN_VERIFY` with a response value computed by the client; (4) the system compares the submitted response against a value it computes from the stored stretched credential and the same nonce, and authenticates the connection only on a match. The nonce SHALL be invalidated after being consumed by one `LOGIN_VERIFY` attempt (successful or not) and SHALL never be reused.

For a name with no matching stored account, `LOGIN_INIT` SHALL respond with a deterministic salt and nonce indistinguishable in structure and timing from a real account's response, so that an observer cannot determine whether a given name is registered. A name belonging to an enrolled user who holds no companion account SHALL be treated the same way, so that the response does not reveal which users are admins.

Every command accepted on the Authentication characteristic SHALL have a defined length, and the system SHALL reject a write whose length does not match the command it carries. No command SHALL be accepted on the basis of an unbounded or characteristic-wide length allowance alone.

A write to the Authentication characteristic SHALL be bounded by the largest well-formed command the characteristic accepts, which is REGISTER at command byte, name length byte, maximum name, and password hash. The system SHALL reject a longer write with an invalid-length error before copying it and before dispatching on its command byte, so that an oversized write is never staged or interpreted.

LOGOUT carries no operands and SHALL be exactly one byte; a LOGOUT write of any other length SHALL be rejected with an invalid-length error and SHALL NOT log the connection out. LOGIN_INIT, LOGIN_VERIFY and REGISTER SHALL each be accepted only at the exact length implied by their own encoding.

The Authentication characteristic's per-command wire format SHALL be documented, and that documentation SHALL match the lengths the system enforces.

#### Scenario: Unauthorized access blocked
- **WHEN** an unauthenticated client attempts to write to the Config characteristic
- **THEN** system returns ESP_GATT_AUTH_FAIL

#### Scenario: LOGIN challenge issued for a registered user
- **WHEN** a client submits `LOGIN_INIT` with a name that has a stored account
- **THEN** system replies with that account's stored salt, the key-derivation iteration count, and a newly generated single-use nonce

#### Scenario: LOGIN challenge issued for an unknown user is indistinguishable
- **WHEN** a client submits `LOGIN_INIT` with a name that has no stored account
- **THEN** system replies with a deterministic salt and a newly generated nonce in the same shape as a registered-user response
- **AND** no subsequent `LOGIN_VERIFY` response can succeed for that name

#### Scenario: A name belonging to a non-admin is indistinguishable from an unknown name
- **WHEN** a client submits `LOGIN_INIT` with the name of an enrolled user who holds no companion account
- **THEN** system replies in the same shape as it would for a name it does not recognize
- **AND** the reply does not reveal that the name belongs to an enrolled user

#### Scenario: Successful authentication
- **WHEN** a client submits `LOGIN_VERIFY` with a response value that matches what the system computes from the stored stretched credential and the outstanding nonce
- **THEN** system transitions the BLE connection to authenticated state
- **AND** system records the fingerprint user id bound to that account on the connection
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

#### Scenario: Registration binds the account to the authorizing admin
- **WHEN** an admin fingerprint scan authorizes a pending registration
- **THEN** the persisted credential is bound to that admin's fingerprint user id
- **AND** the submitted name becomes that user's name

#### Scenario: Re-registration by the same admin replaces the credential
- **WHEN** an admin who already holds an account completes a registration
- **THEN** that user's salt and stretched credential are replaced
- **AND** the user still holds exactly one account
- **AND** a login response computed from the previous credential is rejected

#### Scenario: Admin authority is re-read per decision
- **WHEN** an authenticated connection requests an action that requires admin permission
- **THEN** system reads the current permission of the user bound to that connection
- **AND** permits the action only if that permission is admin

#### Scenario: Demoted user loses authority on an open connection
- **WHEN** the user bound to an open authenticated connection is demoted from admin
- **THEN** subsequent actions on that connection requiring admin permission are refused
- **AND** the account record is not modified to achieve this

#### Scenario: Deleted user cannot authenticate again
- **WHEN** an enrolled user holding an account is deleted
- **THEN** a subsequent `LOGIN_INIT` for that user's name is answered as an unknown name
- **AND** no `LOGIN_VERIFY` for that name can succeed

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

#### Scenario: Enrollment characteristic refused without admin authority outside setup
- **WHEN** an unauthenticated client that is not a setup-phase connection writes to the Enrollment characteristic
- **THEN** system returns an insufficient authentication error
- **AND** no user-management verb is performed

## ADDED Requirements

### Requirement: Enrollment Characteristic Carries A User-Management Request/Reply Protocol

The Enrollment characteristic SHALL accept writes that carry a user-management verb and a client-supplied request id, and SHALL answer every accepted write with exactly one terminal reply notification carrying that same request id.

A reply SHALL be produced for every request, including a request rejected before any work is attempted — malformed payload, unknown verb, or an out-of-range field. The only condition under which no reply is produced SHALL be the loss of the connection that made the request.

Enrolment progress notifications SHALL carry the request id of the request that started the enrolment, so that a client can attribute them without inferring from ordering.

The characteristic SHALL NOT accept a bare enrolment payload carrying only a user id and permission; such a write SHALL be answered as an invalid request.

#### Scenario: Every request is answered
- **WHEN** a client writes a well-formed user-management request
- **THEN** system eventually notifies exactly one terminal reply carrying that request's id

#### Scenario: Malformed request is answered, not dropped
- **WHEN** a client writes a payload that cannot be parsed as a user-management request
- **THEN** system notifies a reply reporting an invalid request
- **AND** no user-management verb is performed

#### Scenario: Unknown verb is answered
- **WHEN** a client writes a request whose verb the system does not implement
- **THEN** system notifies a reply reporting an invalid request

#### Scenario: Enrolment progress is attributable
- **WHEN** an enrolment started by a request emits a step-progress notification
- **THEN** that notification carries the id of the request that started the enrolment

#### Scenario: Legacy bare enrolment payload rejected
- **WHEN** a client writes a payload carrying only a user id and a permission, with no verb and no request id
- **THEN** system answers it as an invalid request
- **AND** no enrolment is started

### Requirement: User-Management Verbs Never Block The BLE Host Task

The system SHALL NOT perform any user-management verb, or wait for its result, on the thread that services GATT access callbacks. A verb whose completion depends on a fingerprint scan SHALL be handed to a task that may wait, and its reply SHALL be delivered by notification when it resolves.

#### Scenario: Permission change does not stall the host task
- **WHEN** a client requests a permission change, which waits for an authorizing admin scan
- **THEN** the GATT write completes without waiting for that scan
- **AND** other characteristics remain serviceable while the scan is outstanding

#### Scenario: Result delivered asynchronously
- **WHEN** the authorizing scan for an outstanding verb resolves
- **THEN** system notifies the terminal reply for that request

### Requirement: Mutating User-Management Verbs Require An Admin Fingerprint Scan

Every user-management verb that changes device state — enrol, delete, change permission, rename — SHALL be authorized by a live admin fingerprint scan resolved through the pending-admin-action gate, in addition to the connection's live admin authority. A verb that only reads state SHALL NOT require a scan.

An authenticated connection alone SHALL NOT be sufficient to enrol a user at any permission level. This closes the path by which a session could create an admin-permission user with nobody present at the device.

A request arriving while another admin action, permission change or enrolment is in flight SHALL be answered as busy rather than queued or discarded.

#### Scenario: Enrolment requires an authorizing scan
- **WHEN** an authenticated admin session requests an enrolment
- **THEN** system arms the admin-fingerprint gate and does not start the enrolment state machine
- **AND** the enrolment begins only after an admin fingerprint scan authorizes it

#### Scenario: Enrolment refused without a matching scan
- **WHEN** the pending admin action for a requested enrolment is denied or times out
- **THEN** no enrolment is started
- **AND** system replies with the denial or timeout reason

#### Scenario: Deletion requires an authorizing scan
- **WHEN** an authenticated admin session requests a deletion
- **THEN** the deletion is performed only after an admin fingerprint scan authorizes it

#### Scenario: Listing requires no scan
- **WHEN** an authenticated admin session requests the user list
- **THEN** system replies without arming the admin-fingerprint gate

#### Scenario: Concurrent request reported as busy
- **WHEN** a client requests a mutating verb while another admin-gated action is already in flight
- **THEN** system replies that the device is busy
- **AND** the in-flight action is unaffected

### Requirement: Setup-Phase Admission To The Enrollment Characteristic

While the device is in the setup phase and no user is enrolled, the Enrollment characteristic SHALL admit the enrolment verb from the setup connection without authentication and without an admin fingerprint scan, because neither an account nor an admin exists yet.

That admission SHALL be limited to the enrolment verb, and SHALL cease as soon as any user is enrolled. Every other verb SHALL be refused on such a connection, and every verb including enrolment SHALL be refused once the device holds an enrolled user or has left the setup phase.

#### Scenario: Wizard enrols the first Admin
- **WHEN** a setup-phase connection to a device with no enrolled users writes an enrolment request
- **THEN** system accepts it and starts the enrolment

#### Scenario: Admission closes after the first enrolment
- **WHEN** the same connection writes a second enrolment request after a user has been enrolled
- **THEN** system refuses it with an insufficient authentication error

#### Scenario: Other verbs are not admitted
- **WHEN** a setup-phase connection to a device with no enrolled users writes a delete, rename, permission-change or list request
- **THEN** system refuses it with an insufficient authentication error

#### Scenario: Admission does not apply to a claimed device
- **WHEN** a connection to a device whose setup is complete writes an enrolment request without live admin authority
- **THEN** system refuses it with an insufficient authentication error
