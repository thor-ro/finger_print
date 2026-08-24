## ADDED Requirements

### Requirement: Scriptable GATT Central Against An Emulated Device

The project SHALL provide a scriptable BLE central that connects to a firmware image running under `esp-emu` and drives the companion OTA wire protocol, with no physical device or host Bluetooth adapter required.

#### Scenario: Central attaches to the emulator

- **WHEN** the emulator is started with an HCI TCP backend and the harness is pointed at that endpoint
- **THEN** the central completes connection setup with the emulated device without using a host Bluetooth adapter

#### Scenario: Runs unattended

- **WHEN** the harness is invoked in CI
- **THEN** it requires no interactive input, bounds every wait with a timeout, and exits non-zero on timeout

### Requirement: Unbonded Central Can Reach The Device

The harness SHALL be able to connect to a device that has no existing bond, without modifying the companion component's advertising or bonding logic.

#### Scenario: Pairing window opened by the fixture

- **WHEN** the fixture app starts
- **THEN** it calls the existing public pairing-window API so the device advertises unfiltered, and the harness connects as an unbonded peer

#### Scenario: Production advertising policy is not weakened

- **WHEN** the harness's needs are met
- **THEN** the allow-list-filtered default advertising path is left unchanged, and only the fixture app opts into the pairing window

### Requirement: Just Works Pairing And Encrypted Link

The harness SHALL establish an encrypted, bonded link using the pairing mode the firmware already configures.

#### Scenario: LE Secure Connections without passkey

- **WHEN** the device requests pairing
- **THEN** the central completes LE Secure Connections Just Works pairing with no passkey or out-of-band data, matching the firmware's no-input/no-output, no-MITM configuration

#### Scenario: Encryption precedes protected access

- **WHEN** the central accesses a characteristic that requires an encrypted link
- **THEN** the link is already encrypted, and the access is not rejected for insufficient encryption

### Requirement: Companion Login

The harness SHALL authenticate at the application layer, because the OTA characteristic rejects writes from a connection that is encrypted but not logged in.

#### Scenario: Register and log in

- **WHEN** the harness has an encrypted link to a fixture with no provisioned user
- **THEN** it registers a user, completes the challenge-response login, and the connection becomes authenticated

#### Scenario: Unauthenticated OTA write is rejected

- **WHEN** the harness writes to the OTA characteristic before completing login
- **THEN** the write is rejected with an insufficient-authentication error and no OTA session is opened

### Requirement: Chunked Transfer Over The OTA Characteristic

The harness SHALL implement the companion OTA wire protocol as the firmware defines it.

#### Scenario: Begin, chunk, end

- **WHEN** the harness transfers an image
- **THEN** it writes a BEGIN carrying the image size, a sequence of CHUNK writes sized to the negotiated ATT MTU, and an END, in that order

#### Scenario: Chunk sizing respects the negotiated MTU

- **WHEN** the ATT MTU has been negotiated
- **THEN** no chunk payload exceeds the maximum the firmware computes for that MTU, and no chunk is empty

### Requirement: Status Notifications Asserted

The harness SHALL subscribe to and assert the status notifications the firmware emits, so that a silent change in reported outcome fails the run.

#### Scenario: Progress and success are observed

- **WHEN** a correctly signed image is transferred
- **THEN** the harness observes a ready status at BEGIN, chunk acknowledgements advancing the offset, and a terminal success status

#### Scenario: Failure is reported with a reason

- **WHEN** a transfer is rejected
- **THEN** the harness observes a terminal failure status carrying an error reason, and asserts on that reason rather than on the absence of success alone

### Requirement: Signature Outcomes Over The Real Transport

The harness SHALL run the same three signature cases as the transport-independent gate, so the production transport path is covered end to end.

#### Scenario: Three cases over BLE

- **WHEN** the correctly signed, tampered and foreign-key images are each transferred over the companion transport
- **THEN** the first commits and the other two are rejected, matching the outcomes the transport-independent gate asserts

#### Scenario: Session recovery over BLE

- **WHEN** a transfer is rejected for a signature failure
- **THEN** a subsequent transfer in the same connection or a reconnected session can begin and commit

### Requirement: Documented Fallback When Emulator BLE Is Insufficient

Because emulator BLE fidelity is unproven, the project SHALL record the outcome of evaluating it rather than leaving a silently abandoned harness.

#### Scenario: Evaluation outcome is recorded

- **WHEN** the evaluation of emulator BLE support concludes
- **THEN** its result is written down, and if the flow cannot be carried, the reason is recorded together with the decision to cover the BLE transport on hardware instead

#### Scenario: The signature gate survives the fallback

- **WHEN** the BLE harness is not viable under emulation
- **THEN** the transport-independent signature gate remains the required CI check, so signature verification is still covered without hardware
