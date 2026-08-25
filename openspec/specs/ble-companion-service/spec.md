# Spec: ble-companion-service

## Purpose
This specification covers the BLE Companion Service, which acts as a GATT server on the shared NimBLE host to enable the Web Companion App to connect, authenticate, manage configuration, and trigger OTA updates.

## Requirements

### Requirement: Shared NimBLE Lifecycle
The Companion Service SHALL register its GATT database with the existing NimBLE host before that host starts. `sdf_protocol_ble` SHALL be the sole owner of NimBLE initialization, host-task creation, and host lifecycle callbacks.

The Companion Service SHALL NOT call into the NimBLE host — including any read of the persisted bond store — before that host has been initialized. Registering a GATT database before host start is permitted and required; reading host-owned state before host start is not, because NimBLE store reads acquire the host lock and an uninitialized host lock is fatal rather than an error return.

Any Companion Service startup step that depends on host-owned state SHALL run after host initialization, and SHALL degrade to a logged, non-fatal outcome if it cannot complete, so that a failure in such a step leaves the device running rather than aborting application init.

#### Scenario: Companion and Nuki roles start together
- **WHEN** the application initializes BLE
- **THEN** the Companion Service registers before the shared NimBLE host starts
- **AND** the Nuki client and Companion Service operate as central and peripheral roles on that single host

#### Scenario: Bond store is only read once the host owns it
- **WHEN** the Companion Service seeds its allow list from persisted admission records intersected with NimBLE's persisted bond store
- **THEN** the bond store read happens only after the shared NimBLE host has been initialized
- **AND** application init completes without aborting

#### Scenario: Bond store seeding fails
- **WHEN** seeding the allow list from the persisted bond store cannot run or returns an error
- **THEN** the device completes application init with an empty allow list and logs the failure
- **AND** the device remains reachable through the admin-gated pairing window rather than becoming unbootable

#### Scenario: Bonded companion reconnects after a reboot
- **WHEN** a companion device that was bonded and admitted before a reboot attempts to reconnect
- **THEN** the allow list has already been seeded with that identity by the time the Companion Service accepts filtered connections
- **AND** the reconnect succeeds without re-pairing

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

### Requirement: BLE GATT Authentication
The system SHALL expose an Authentication characteristic supporting a two-step challenge-response LOGIN and a single-step REGISTER. Until a LOGIN challenge is successfully verified on this characteristic, all other restricted characteristics (Config, Enrollment, OTA) SHALL return insufficient authentication errors.

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

### Requirement: OTA Triggering via BLE
The system SHALL expose an OTA characteristic that accepts a chunked binary firmware transfer from an authenticated client over the existing BLE GATT connection. The firmware SHALL NOT establish any Wi-Fi or network connection to perform an OTA update, and SHALL stream the received bytes through the existing signed OTA verification flow.

A transfer SHALL begin with a control message declaring the total firmware image size, followed by one or more binary chunk writes sized to the connection's negotiated ATT MTU, and SHALL end with a control message that triggers integrity and signature verification of the fully-received image before commit. The system SHALL report per-chunk acknowledgement (confirmed byte offset) and final pass/fail verification status back to the client over the existing OTA notification path.

#### Scenario: OTA Triggered
- **WHEN** an authenticated client writes a begin-transfer message declaring the firmware image size to the OTA characteristic
- **THEN** system opens a new OTA session (`SDF_OTA_SOURCE_BLE`) sized to the declared image size
- **AND** system accepts subsequent chunk writes, appending each to the OTA session and acknowledging the confirmed byte offset

#### Scenario: OTA transfer completed and verified
- **WHEN** an authenticated client writes an end-transfer message after sending exactly the declared number of bytes
- **THEN** system verifies the accumulated image's integrity and ECDSA P-256 signature
- **AND** system commits and reports success only if verification passes, or reports failure and does not commit if verification fails

#### Scenario: Malformed or oversized OTA request rejected
- **WHEN** a client writes a malformed begin-transfer message, an oversized declared image size, or chunk data exceeding the negotiated MTU
- **THEN** system rejects the write
- **AND** system does not open or continue an OTA session

#### Scenario: BLE disconnect during transfer allows resume
- **WHEN** an authenticated client that was mid-transfer reconnects and re-authenticates before the in-progress OTA session times out
- **THEN** system reports the currently confirmed byte offset
- **AND** system accepts further chunk writes continuing from that offset without restarting the transfer

#### Scenario: Abandoned OTA session times out
- **WHEN** no chunk write is received for an in-progress OTA session within the configured idle timeout
- **THEN** system aborts the OTA session
- **AND** system releases the update partition so a subsequent OTA attempt can begin cleanly

### Requirement: Persisted Notification Subscription Capacity
The system SHALL size its persisted notification-subscription (CCCD) storage capacity to cover the worst case of every bonded peer subscribing to every NOTIFY-capable characteristic exposed by the Companion Service. The configured CCCD capacity SHALL be greater than or equal to the product of the maximum number of bonded peers and the number of NOTIFY-capable characteristics.

#### Scenario: All bonded peers subscribed to all notify characteristics
- **WHEN** the maximum number of bonded peers are each subscribed to every NOTIFY-capable characteristic
- **THEN** every subscription persists successfully across reconnects
- **AND** no persisted subscription is silently dropped due to exhausted CCCD storage capacity

#### Scenario: Adding a NOTIFY-capable characteristic requires capacity review
- **WHEN** a new NOTIFY-capable characteristic is added to the Companion Service's GATT database
- **THEN** the persisted CCCD capacity SHALL be re-verified to still cover the updated bonded-peer × NOTIFY-characteristic product

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

### Requirement: Sparse, Allow-List-Filtered Advertising
Once the device's setup-completion latch is set, the Companion Service SHALL advertise using a sparse duty cycle and an accept/allow-list filter policy by default, such that only bonded identities already present on the allow list can complete a connection.

While the setup-completion latch is unset and the setup phase is armed, the Companion Service SHALL instead advertise connectably and unfiltered so that an unbonded companion client can reach the first-time setup wizard. While the setup phase is disarmed and the latch is unset, the Companion Service SHALL NOT advertise at all.

#### Scenario: Unknown device cannot connect
- **WHEN** a BLE device not present on the allow list attempts to connect while the Companion Service is in its default advertising mode
- **THEN** the connection attempt SHALL NOT succeed

#### Scenario: Allow-listed device reconnects normally
- **WHEN** a BLE device already present on the allow list attempts to connect while the Companion Service is in its default advertising mode
- **THEN** the connection SHALL be accepted and proceed through the existing bonded/encrypted link flow

#### Scenario: Unclaimed device is reachable by any client
- **WHEN** the setup-completion latch is unset and the setup phase is armed
- **THEN** advertising is connectable and unfiltered
- **AND** a client with no prior bond can connect

#### Scenario: Disarmed unclaimed device is silent
- **WHEN** the setup-completion latch is unset and the setup phase has been disarmed by a timeout
- **THEN** the Companion Service does not advertise
- **AND** no client can connect until a button press re-arms the setup phase

#### Scenario: Completion switches advertising modes
- **WHEN** the setup-completion latch is set at the end of the setup phase
- **THEN** the Companion Service leaves unfiltered advertising and begins sparse, allow-list-filtered advertising with the newly admitted identity on the allow list

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

### Requirement: Setup State Is Observable Before Authentication
The Companion Service SHALL make the device's current setup state observable by a companion client that has not completed login, so that the first-time setup wizard can determine which phase the device is in before any account exists. The exposed state SHALL distinguish at minimum: setup not started, Admin enrolled, companion account registered, Nuki paired, and setup complete. Distinguishing account registration from Admin enrolment is required: without it the state reported for a device with a paired Nuki but no account is indistinguishable from one that has neither, and the wizard resumes at a step the user has already finished.

Exposing setup state SHALL NOT expose any other restricted information, and SHALL NOT weaken the authentication requirement on the Config, Enrollment, and OTA characteristics.

#### Scenario: Wizard reads setup state before registering
- **WHEN** a companion client connects to a device in the setup phase and has not logged in
- **THEN** it can read the device's current setup state
- **AND** it can determine whether an Admin has been enrolled and whether Nuki is paired

#### Scenario: Setup state does not unlock restricted characteristics
- **WHEN** a client reads setup state without having logged in
- **THEN** writes to the Config, Enrollment, and OTA characteristics still return insufficient authentication errors

#### Scenario: Setup state reported on a completed device
- **WHEN** an authenticated client reads setup state on a device whose setup-completion latch is set
- **THEN** the reported state is setup complete
