## ADDED Requirements

### Requirement: Wizard Asks For One Scan At A Time

During the wizard's Admin enrolment step, the app SHALL ask for a single fingerprint scan at a time rather than stating a total up front and then going silent. It SHALL name the scan it is currently waiting for, and on each scan the device reports as captured it SHALL update the visualization and ask for the next scan, until the enrolment reports its outcome.

The visualization SHALL show the captured scans discretely — one marker per required scan, each marker showing whether that scan is captured, currently expected, or still outstanding — so the user can see the count without reading the label. A progress bar alone SHALL NOT be the only representation of the count.

The app SHALL NOT report a scan as captured until the device says so. Progress SHALL be driven by the device's notifications, not by a local timer or by optimistic advance after a prompt is shown.

When the device does not report progress, the app SHALL still show the enrolment as in flight and SHALL state the number of scans required, so an older device that reports only the outcome remains usable.

#### Scenario: The first scan is asked for by name

- **WHEN** the wizard starts the Admin enrolment
- **THEN** the app asks the user to place the Admin finger for the first scan
- **AND** the visualization shows no scan captured yet

#### Scenario: A captured scan advances the prompt

- **WHEN** the device reports that a scan was captured and another is expected
- **THEN** the visualization marks that scan captured
- **AND** the app asks the user for the next scan by its number

#### Scenario: The last scan completes the step

- **WHEN** the device reports the enrolment succeeded
- **THEN** the visualization shows every scan captured
- **AND** the wizard advances to account registration

#### Scenario: A failed enrolment does not leave a stale prompt

- **WHEN** the device reports the enrolment failed
- **THEN** the app stops asking for a scan
- **AND** reports the failure with the step it failed at

#### Scenario: A device that reports no progress remains usable

- **WHEN** the enrolment is started and the device sends no progress notification
- **THEN** the app still shows the enrolment as in flight and states how many scans are required
