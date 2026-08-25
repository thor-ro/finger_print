# Spec: device-setup-phase

## Purpose

Defines the lifecycle of a device that has not yet been claimed: when it is open for an app-guided setup wizard, how that openness is bounded in time and in concurrency, how setup completion is recorded, and how the device returns to an unclaimed state.

## Requirements

### Requirement: Setup Phase Is The Unclaimed Device's Operating Mode
A device whose setup-completion latch is not set SHALL operate in a setup phase, during which the BLE Companion Service advertises connectably and without allow-list filtering so that an arbitrary companion client can connect and run the first-time setup wizard. Access to the setup phase SHALL NOT be gated on an Admin fingerprint, since no Admin exists until the wizard enrols one.

The setup phase SHALL be armed on first boot of an unprovisioned device, and SHALL be armed on completion of a factory reset. Once disarmed, the setup phase SHALL be re-armable only by a physical button press.

#### Scenario: Unprovisioned device is open for setup
- **WHEN** a device boots with its setup-completion latch unset and the setup phase armed
- **THEN** the Companion Service advertises connectably with no allow-list filter
- **AND** a companion client with no prior bond can connect and run the wizard

#### Scenario: First boot arms the setup phase
- **WHEN** a device boots for the first time with no persisted setup state
- **THEN** the setup phase is armed without requiring a button press

#### Scenario: Completed device does not enter the setup phase
- **WHEN** a device boots with its setup-completion latch set
- **THEN** the setup phase SHALL NOT be armed
- **AND** advertising follows the sparse, allow-list-filtered default

### Requirement: Setup Phase Accepts At Most One Connection
While the setup phase is armed, the Companion Service SHALL accept at most one connection at a time. A second inbound connection SHALL be rejected by terminating it at connection establishment, rather than relying on advertising not being re-armed while a connection is up. The connection limit SHALL be derived from the device's setup state, so that the ordinary multi-connection limit applies again once setup is complete.

#### Scenario: Second connection during setup is rejected
- **WHEN** a client is already connected during the setup phase
- **AND** a second client establishes a connection
- **THEN** the system terminates the second connection immediately
- **AND** the first client's session is unaffected

#### Scenario: Ordinary connection limit restored after completion
- **WHEN** the setup-completion latch is set
- **THEN** the Companion Service accepts up to its ordinary maximum number of concurrent connections

### Requirement: Physical Button Reclaims The Setup Connection
While the setup phase is armed, a button press SHALL terminate the current setup connection, if any, and re-arm advertising, so that a person physically present at the device can always take the setup slot from a client that is occupying it. A button press while the setup phase is disarmed SHALL re-arm it.

#### Scenario: Occupied setup slot is reclaimed
- **WHEN** a client holds the single setup connection
- **AND** a button press occurs on the physical device
- **THEN** the system terminates that connection
- **AND** resumes unfiltered, connectable advertising so another client can connect

#### Scenario: Lapsed setup phase is re-armed
- **WHEN** the setup phase is disarmed
- **AND** a button press occurs on the physical device
- **THEN** the system arms the setup phase and resumes unfiltered, connectable advertising

#### Scenario: Setup-phase button press triggers no admin action
- **WHEN** a button press occurs while the setup phase is armed
- **THEN** no admin action is requested and no pending admin action is set

### Requirement: Setup Ends Only By Explicit Completion Or Timeout
The setup phase SHALL end in exactly one of two ways: an explicit completion request issued by the companion app over an authenticated session, or expiry of the setup timeout. Completion SHALL NOT be inferred from any individual setup step, including persistence of Nuki credentials or enrolment of an Admin user.

#### Scenario: App completes setup explicitly
- **WHEN** an authenticated companion client issues the setup-completion request
- **AND** the prerequisites for completion are satisfied
- **THEN** the system sets the setup-completion latch, admits the requesting client, enables allow-list-filtered advertising, and leaves the setup phase

#### Scenario: Persisting Nuki credentials does not complete setup
- **WHEN** Nuki credentials are persisted during the wizard
- **AND** no completion request has been issued
- **THEN** the device remains in the setup phase

#### Scenario: Completion request without prerequisites is rejected
- **WHEN** a completion request is issued before an Admin user has been enrolled
- **THEN** the system rejects the request and remains in the setup phase

### Requirement: Completion Requires Every Wizard Step And Applies Atomically
Completion SHALL be accepted only when every wizard step has been performed: an Admin user enrolled, a companion account registered, and Nuki credentials persisted. The system SHALL NOT accept completion on the strength of any subset — in particular it SHALL NOT complete setup on a device with no companion account, which would leave the device claimed with nobody able to log in.

A rejection SHALL name the first outstanding step so the app can return the user to it. A failure that is not an unmet prerequisite SHALL be reported distinctly from an outstanding step, so the user is not sent back to redo work that succeeded.

Completion SHALL take effect as a whole or not at all. If any part of applying completion fails after the setup-completion latch has been persisted, the system SHALL clear the latch again, leaving the device in the setup phase and completable on retry. The system SHALL NOT come to rest with the latch set and the setup phase still armed: the running setup deadline would then wipe accounts and admission records out from under a device that reports itself claimed, leaving it reachable only by a physical factory reset.

#### Scenario: Completion without a companion account is rejected
- **WHEN** a completion request is issued on a device with an enrolled Admin and persisted Nuki credentials but no registered companion account
- **THEN** the system rejects the request and names account registration as the outstanding step
- **AND** the setup-completion latch remains unset

#### Scenario: Failure after latching rolls the latch back
- **WHEN** the setup-completion latch has been persisted and a subsequent part of completion fails
- **THEN** the system clears the setup-completion latch
- **AND** the device remains in the setup phase with the completion request reported as failed
- **AND** a later completion request can succeed

#### Scenario: Internal failure is not reported as an outstanding step
- **WHEN** a completion request fails for a reason other than an unmet prerequisite
- **THEN** the response distinguishes that failure from the named wizard steps

### Requirement: Setup Phase Is Bounded By An Arm Window And A Setup Deadline
The setup phase SHALL be bounded by two compile-time timers.

The **arm window** SHALL run from the moment the setup phase is armed and SHALL bound how long the device advertises openly with no client connected. Its default SHALL be 5 minutes. Once the setup deadline has started, the arm window SHALL stop governing the setup phase and SHALL NOT be restarted by a disconnection, an idle-connection drop, or a reconnection; the setup deadline is then the sole bound. Worst-case open advertising per arming is therefore the arm window plus the setup deadline, reached only when a client connects in the final instant of the arm window.

The **setup deadline** SHALL run from the first connection accepted during the setup phase and SHALL bound how long a user has to complete the wizard. Its default SHALL be 10 minutes. The setup deadline SHALL NOT be extended or restarted by client activity, setup progress, disconnection, or reconnection.

Expiry of either timer SHALL end the setup phase as described in "Setup Timeout Wipes Partial State Without Resume".

Separately, a **connection idle timer**, default 2 minutes, SHALL terminate a setup connection that has shown no GATT activity, and SHALL re-arm advertising so another client can take the slot. Expiry of the idle timer SHALL terminate only the connection; it SHALL NOT end the setup phase, wipe state, or affect the setup deadline.

A button press during the setup phase SHALL restart both the arm window and the setup deadline. This is the only way either timer is extended, and it requires physical presence at the device.

#### Scenario: Arm window expires with no client
- **WHEN** the setup phase is armed and no client connects before the arm window elapses
- **THEN** the setup phase ends and the device stops advertising

#### Scenario: Connecting starts the setup deadline
- **WHEN** a client connects during the setup phase
- **THEN** the setup deadline begins running from that connection
- **AND** the arm window no longer governs the setup phase

#### Scenario: Reconnecting does not extend the deadline
- **WHEN** a client disconnects and reconnects before the setup deadline elapses
- **THEN** the setup deadline still expires at its original time

#### Scenario: Disconnecting does not restart the arm window
- **WHEN** a client connects during the setup phase and later disconnects or is dropped as idle
- **THEN** the arm window does not restart
- **AND** the setup phase ends at the original setup deadline even if no client ever reconnects

#### Scenario: Progress does not extend the deadline
- **WHEN** a client authenticates, enrols an Admin, and pairs a Nuki during the setup phase
- **THEN** the setup deadline still expires at its original time

#### Scenario: Idle connection is dropped without ending setup
- **WHEN** a setup connection shows no GATT activity for the connection idle timer
- **THEN** the system terminates that connection and re-arms advertising
- **AND** the setup phase remains armed with the setup deadline still running
- **AND** no partial setup state is erased

#### Scenario: Button press restarts both timers
- **WHEN** a button press occurs during the setup phase
- **THEN** the arm window and the setup deadline both restart
- **AND** no partial setup state is erased

### Requirement: Setup Timeout Wipes Partial State Without Resume
When the setup phase ends by expiry of the arm window or the setup deadline, the system SHALL erase all partial setup state — enrolled fingerprint templates, web companion accounts, persisted bonds, admission records, and any persisted Nuki credentials — disarm the setup phase, and stop advertising. The system SHALL NOT preserve any partial progress for a later resume; a subsequent setup attempt starts from an unprovisioned device.

#### Scenario: Expiry erases partial progress
- **WHEN** the arm window or the setup deadline expires with the setup-completion latch unset
- **THEN** enrolled templates, companion accounts, bonds, admission records, and any persisted Nuki credentials are erased
- **AND** the setup-completion latch remains unset

#### Scenario: Expiry stops advertising
- **WHEN** the arm window or the setup deadline expires
- **THEN** the system disarms the setup phase and stops advertising
- **AND** the device does not advertise again until a button press re-arms the setup phase

#### Scenario: Re-armed setup starts from scratch
- **WHEN** the setup phase is re-armed after an expiry
- **THEN** the wizard begins at Admin enrolment, with no partial state carried over

#### Scenario: Template erasure failure is non-fatal
- **WHEN** erasing enrolled fingerprint templates from the sensor fails during a timeout wipe
- **THEN** the failure is logged and the device still disarms the setup phase and stops advertising
- **AND** the device remains operable rather than aborting

### Requirement: Setup Completion Is A Latched Flag
Setup completion SHALL be recorded as a single persisted latch, written once at explicit completion. It SHALL NOT be derived from enrolled-user count, persisted Nuki credentials, or any combination of independently-mutable state.

Once set, the latch SHALL be cleared only by a factory reset. Deleting the last enrolled user, clearing Nuki credentials, or evicting every bonded companion SHALL NOT clear it and SHALL NOT return a device in service to the setup phase.

#### Scenario: Latch survives deletion of the last admin
- **WHEN** every enrolled user is deleted from a device whose setup-completion latch is set
- **THEN** the latch remains set
- **AND** the device does not enter the setup phase or resume unfiltered advertising

#### Scenario: Latch survives clearing Nuki credentials
- **WHEN** persisted Nuki credentials are cleared from a device whose setup-completion latch is set
- **THEN** the latch remains set and the device stays out of the setup phase

#### Scenario: Factory reset clears the latch
- **WHEN** a factory reset completes
- **THEN** the setup-completion latch is cleared and the setup phase is armed

### Requirement: Completion Persistence Order Is Crash-Safe
The system SHALL persist the admitting client's admission record before persisting the setup-completion latch, so that an interruption between the two writes leaves the device in the setup phase rather than in a completed state with no admitted companion.

#### Scenario: Power loss between completion writes
- **WHEN** power is lost after the admission record is written and before the setup-completion latch is written
- **THEN** the device boots into the setup phase
- **AND** the wizard can reach it and complete setup again

#### Scenario: Completed device always has an admitted companion
- **WHEN** a device boots with its setup-completion latch set
- **THEN** at least one admission record was persisted before that latch was written

### Requirement: Factory Reset Requires No Admin Fingerprint
Factory reset SHALL be triggerable by its physical button gesture alone, without Admin fingerprint authorization, because it is the recovery path for a lost or unreadable Admin fingerprint. Factory reset SHALL erase enrolled users, web companion accounts, persisted bonds, admission records, Nuki credentials, and Zigbee state, SHALL clear the setup-completion latch, and SHALL arm the setup phase.

#### Scenario: Factory reset without an admin present
- **WHEN** the factory-reset button gesture occurs on a device whose Admin fingerprint can no longer be read
- **THEN** the reset proceeds without requiring a fingerprint scan
- **AND** the device erases all persisted state and arms the setup phase

#### Scenario: Factory reset sets no pending admin action
- **WHEN** the factory-reset button gesture occurs
- **THEN** no pending admin action is set and no Admin fingerprint scan is awaited

#### Scenario: Reset device is re-claimable
- **WHEN** a factory reset completes
- **THEN** a companion client can connect and run the wizard as on an unprovisioned device
