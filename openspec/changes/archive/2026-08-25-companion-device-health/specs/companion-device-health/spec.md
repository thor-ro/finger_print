## ADDED Requirements

### Requirement: The Companion Can Observe The Device's Health

The system SHALL report its current health to an authorized companion session, covering at minimum: the lock's state, the battery level, the active alarm conditions, whether the fingerprint sensor is responding, whether the lock link is paired and connected, whether the radio has joined a network where one applies, the running firmware version, the current OTA state, and the setup state.

The report SHALL be a snapshot of the present, not a history. It SHALL be obtainable on demand and SHALL be delivered again when a reported value changes.

The report SHALL NOT carry secret material — no authorization identifiers, no keys, no credential salts, and no enrolled-user records.

#### Scenario: Health reported on demand
- **WHEN** an authorized companion session requests the device's health
- **THEN** system reports the current value of every field in the report

#### Scenario: Health reported again on change
- **WHEN** a reported value changes while an authorized session is subscribed
- **THEN** system delivers an updated report without the session having to ask

#### Scenario: No secrets in the report
- **WHEN** the health report is produced
- **THEN** it contains no authorization identifier, key, credential salt or user record

### Requirement: Unmeasured Values Are Reported As Unknown, Never Substituted

Every field in the health report SHALL be in exactly one of three conditions: a measured value, unknown, or not applicable. A field SHALL NOT be populated with a substitute for a value the system does not have.

"Unknown" SHALL mean the system holds no reading — the measurement failed, or nothing has reported the value since boot. "Not applicable" SHALL mean the subsystem is absent by build or configuration. Neither SHALL be represented as a number that a reader could mistake for a measurement.

A configured default SHALL NOT be reported as a measurement of the thing it is a default for. In particular, the configured default battery percentage SHALL NOT be reported or displayed as the device's battery level.

#### Scenario: Missing reading reported as unknown
- **WHEN** the system has no reading for a reported value
- **THEN** the report states that the value is unknown
- **AND** it does not carry a number for that value

#### Scenario: Absent subsystem reported as not applicable
- **WHEN** a reported subsystem is absent by build or configuration
- **THEN** the report distinguishes that from the subsystem being present and unreadable

#### Scenario: Configured default is not a measurement
- **WHEN** the health report carries a battery level
- **THEN** it is a measurement
- **AND** it is not the configured default battery percentage

### Requirement: A Failed Battery Measurement Is Not Treated As A Full Battery

The battery measurement interface SHALL report unavailability distinctly from a reading. An uninitialised sensor, a failed read, and a build without battery sensing SHALL each report unavailability rather than a value.

No consumer SHALL act on an unavailable measurement as though the battery were healthy. The low-battery warning SHALL be raised when the battery is measured and low, SHALL NOT be raised when the measurement is unavailable, and the unavailability SHALL itself be reportable.

#### Scenario: Uninitialised sensor reports unavailability
- **WHEN** the battery level is requested and the sensor was never initialised
- **THEN** the interface reports that no measurement is available
- **AND** it does not report a full battery

#### Scenario: Failed read reports unavailability
- **WHEN** a battery read fails
- **THEN** the interface reports that no measurement is available

#### Scenario: Low-battery warning is not raised on an unavailable measurement
- **WHEN** a lock action is authorized and no battery measurement is available
- **THEN** the low-battery warning is not raised
- **AND** the health report states the battery level is unknown

#### Scenario: Low-battery warning still raised on a measured low battery
- **WHEN** a lock action is authorized and the measured battery level is at or below the low-battery threshold
- **THEN** the low-battery warning is raised

### Requirement: Last-Known Device State Is Held Independently Of Any Transport

The system SHALL hold its last-known device state in one place that does not depend on any reporting transport being enabled. Disabling a transport SHALL NOT cause the state to stop being recorded, and SHALL NOT change what another transport reports.

Every transport that reports these values SHALL read them from that one place, so that two transports cannot report different values for the same thing at the same time.

#### Scenario: State recorded with a transport disabled
- **WHEN** the device learns its lock state or battery level while a reporting transport is disabled
- **THEN** the value is recorded
- **AND** it is available to the report the companion reads

#### Scenario: Transports agree
- **WHEN** two transports report the same device value
- **THEN** they report the same value, because both read it from the same place

#### Scenario: Console agrees with the companion
- **WHEN** the console prints the last known lock state
- **THEN** it prints the recorded value rather than reporting it as unknown while the companion shows it

### Requirement: Lock State Carries Its Provenance And Its Age

Where the system reports a lock state, it SHALL state whether that state was confirmed by the lock or assumed from a command the system sent and that the lock has not yet confirmed. A state derived from having issued a lock or unlock command SHALL NOT be presented as equivalent to one the lock reported.

Every value in the report that can become stale SHALL carry the age of the reading. A value that cannot become stale SHALL carry no age rather than a fabricated one.

#### Scenario: Confirmed state marked as confirmed
- **WHEN** the lock reports its state and the system records it
- **THEN** the health report marks that state as confirmed by the lock

#### Scenario: Assumed state marked as assumed
- **WHEN** the system records a lock state because it issued a lock or unlock command
- **THEN** the health report marks that state as assumed and not yet confirmed

#### Scenario: Confirmation replaces the assumption
- **WHEN** the lock confirms a state after the system assumed one
- **THEN** the report carries the confirmed state and is marked as confirmed

#### Scenario: Age accompanies a stale-able reading
- **WHEN** the report carries a lock state or a battery level
- **THEN** it carries how long ago that reading was taken

### Requirement: Producing The Health Report Performs No Sensor Or Bus I/O

Producing the health report SHALL NOT initiate a fingerprint sensor transaction, a lock-link exchange, a persistent-storage read, or any other bus operation. The report SHALL be produced from recorded state.

Fingerprint sensor readiness SHALL be published by the fingerprint path when it performs I/O for its own reasons, and SHALL NOT be probed on behalf of a reader, so that a reader cannot interleave a sensor transaction with an enrolment or a match, per `fingerprint-io — Fingerprint Operations Serialize Instead of Failing Under Contention`.

A reader SHALL NOT be able to affect the timing of any subsystem by requesting the report, however often it requests it.

#### Scenario: Report does not probe the fingerprint sensor
- **WHEN** the health report is produced
- **THEN** no fingerprint sensor transaction is initiated

#### Scenario: Report during an enrolment does not disturb it
- **WHEN** the health report is requested while an enrolment is in progress
- **THEN** the enrolment is unaffected
- **AND** the report states the recorded sensor readiness

#### Scenario: Repeated requests do not initiate work
- **WHEN** the health report is requested repeatedly
- **THEN** no additional sensor or bus operation is initiated by any of the requests
