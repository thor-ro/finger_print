## MODIFIED Requirements

### Requirement: Static App Location
The Web App SHALL be maintained as source in the repository's `web-companion/` directory and SHALL be compiled by that directory's build to static assets that require no server-side runtime and no third-party origin at run time, suitable for GitHub Pages deployment.

The build output — not the source directory — SHALL be the deployment artifact. The source directory SHALL carry the instructions needed to install the toolchain, build the app, and deploy the result.

#### Scenario: GitHub Pages deployment assets are present
- **WHEN** the repository is prepared for deployment
- **THEN** `web-companion/` contains the source, build configuration, and deployment instructions needed to produce and host the app
- **AND** the assets uploaded to the host are the build's output rather than the source files

#### Scenario: Built assets need no runtime dependencies
- **WHEN** the built assets are served by a static file host
- **THEN** the app runs without a server-side component
- **AND** it loads no code, styles or fonts from any other origin

### Requirement: Refusals Are Rendered Specifically

The Web App SHALL render the specific reason a user-management request was refused — that it would leave no admin, that the name is taken, that the target id is already enrolled, that the device is busy, that the scan was denied, or that no scan arrived in time — rather than a generic failure message.

Every outcome the device is capable of reporting SHALL have a message written for it, including its generic failure and unavailable outcomes. The app SHALL NOT present a raw protocol token to the user as the explanation of a refusal.

#### Scenario: Last-admin refusal explained
- **WHEN** the device refuses a delete or demotion because it would leave no admin
- **THEN** the app says so, rather than reporting a generic error

#### Scenario: Name conflict explained
- **WHEN** the device refuses a rename or enrolment because another user holds that name
- **THEN** the app says the name is already in use

#### Scenario: Busy explained as retryable
- **WHEN** the device reports it is busy with another action
- **THEN** the app says so and invites the user to retry, rather than reporting a failure

#### Scenario: Denied and timed-out scans explained differently
- **WHEN** an authorizing scan is denied
- **THEN** the app's message differs from the one it shows when no scan arrived in time

#### Scenario: Every device outcome has a message
- **WHEN** the device reports any outcome its firmware can produce, including a generic failure or an unavailable subsystem
- **THEN** the app shows a message written for that outcome
- **AND** the raw protocol token is not shown to the user as the explanation

### Requirement: OTA Battery Warning
The Web App SHALL warn the user before triggering an OTA update about potential battery drain and SHALL stream the selected firmware image as a chunked binary transfer over the OTA characteristic using the begin/chunk/end opcode contract.

The warning SHALL state the device's reported battery level, taken from the device health report, rather than asking the user to establish it by other means. Where the battery level is unknown, the warning SHALL say so, and SHALL NOT imply that the level has been checked.


The app SHALL NOT re-send a chunk whose acknowledgement did not arrive. A chunk message carries no offset, so a device that wrote the chunk and lost only the acknowledgement would append the same bytes twice. The app SHALL instead re-establish the device's confirmed offset by re-sending the begin-transfer message, and SHALL continue from the offset the device reports.

#### Scenario: A lost acknowledgement never duplicates bytes
- **WHEN** a chunk message draws no response within the app's response timeout
- **THEN** app does not re-send that chunk
- **AND** app re-sends the begin-transfer message with the same declared image size and continues from the offset the device reports in its `ready` response

#### Scenario: A late acknowledgement is not read as the resume response
- **WHEN** the acknowledgement of a timed-out chunk arrives while the app is waiting for its begin-transfer response
- **THEN** app discards that acknowledgement rather than treating it as the device's confirmed resume offset
#### Scenario: OTA Warning
- **WHEN** user selects a firmware file and starts an OTA update
- **THEN** app displays a warning: "Ensure your battery is above 20%. OTA transfer over Bluetooth draws significant power and firmware is large — keep the app open and the device nearby until it completes."
- **AND** app writes a begin-transfer message (`0x01`, 4-byte little-endian image size) to the OTA characteristic after user confirmation
- **AND** the warning states the device's reported battery level alongside that text

#### Scenario: Unknown battery stated as unknown
- **WHEN** user starts an OTA update and the device reports no battery measurement
- **THEN** app states that the battery level is unknown
- **AND** app does not display a battery percentage in the warning

#### Scenario: Chunked transfer with progress
- **WHEN** the begin-transfer message is acknowledged with a `ready` status
- **THEN** app writes the firmware image as a sequence of chunk messages (`0x02` + raw bytes), each sized to the connection's negotiated ATT MTU minus 4 bytes of overhead
- **AND** app updates a progress indicator from each `chunk_ack` notification's confirmed byte offset against the declared image size
- **AND** app writes an end-transfer message (`0x03`, no payload) once all bytes have been sent

#### Scenario: Transfer completion is inferred, not just notified
- **WHEN** the app has written the end-transfer message
- **THEN** app shows a "verifying and installing, device will restart" status and starts a bounded grace period
- **AND** if a `failed` notification arrives within the grace period, app reports the failure and its error to the user
- **AND** if instead the BLE connection drops within the grace period without a `failed` notification, app treats this as a presumed success (the device's successful-commit path reboots before it can send a `success` notification) and prompts the user to reconnect to confirm the new firmware version
- **AND** if neither a `failed` notification nor a disconnect occurs before the grace period elapses, app reports an ambiguous/unknown outcome rather than guessing

#### Scenario: Resume after disconnect
- **WHEN** the BLE connection drops mid-transfer and the app reconnects and re-authenticates before the device's OTA idle timeout elapses
- **THEN** app re-sends the begin-transfer message with the same declared image size
- **AND** app resumes chunk-sending from the byte offset returned in the `ready` response instead of restarting the transfer from byte 0

## ADDED Requirements

### Requirement: An Action's Outcome Is Reported Where The Action Was Taken

The Web App SHALL report the outcome of an action in the part of the interface from which that action was initiated, so that a message cannot be read as belonging to an unrelated operation.

In particular, the outcome of reading or writing device configuration SHALL NOT be reported in the firmware-update area of the interface.

#### Scenario: Configuration outcome appears with the configuration controls
- **WHEN** the user reads or applies device configuration
- **THEN** the resulting message is shown with the configuration controls
- **AND** it does not appear in the firmware-update area

#### Scenario: Firmware-update area reports only firmware updates
- **WHEN** the firmware-update area shows a status message
- **THEN** that message concerns the firmware update
