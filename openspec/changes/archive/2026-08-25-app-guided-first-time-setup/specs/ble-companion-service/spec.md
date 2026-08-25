## MODIFIED Requirements

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
- **WHEN** seeding the allow list cannot run or returns an error
- **THEN** the device completes application init with an empty allow list and logs the failure
- **AND** the device remains reachable through the admin-gated pairing window rather than becoming unbootable

#### Scenario: Bonded companion reconnects after a reboot
- **WHEN** a companion device that was bonded and admitted before a reboot attempts to reconnect
- **THEN** the allow list has already been seeded with that identity by the time the Companion Service accepts filtered connections
- **AND** the reconnect succeeds without re-pairing

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

## ADDED Requirements

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
