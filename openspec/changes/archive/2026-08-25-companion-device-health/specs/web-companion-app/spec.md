## MODIFIED Requirements

### Requirement: OTA Battery Warning
The Web App SHALL warn the user before triggering an OTA update about potential battery drain and SHALL stream the selected firmware image as a chunked binary transfer over the OTA characteristic using the begin/chunk/end opcode contract.

The warning SHALL state the device's reported battery level, taken from the device health report, rather than asking the user to establish it by other means. Where the battery level is unknown, the warning SHALL say so, and SHALL NOT imply that the level has been checked.

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

## ADDED Requirements

### Requirement: Device Health View

The Web App SHALL present the device health report to an authenticated session, covering the lock state, the battery level, active alarms, fingerprint sensor readiness, the lock link, the radio's network state, the running firmware version and the OTA state.

The view SHALL update from the device's health notifications rather than by polling, and SHALL be available to any authenticated user, not only an admin.

#### Scenario: Health shown after authentication
- **WHEN** a user authenticates
- **THEN** the app presents the device health report

#### Scenario: View updates on notification
- **WHEN** the device notifies a changed health report
- **THEN** the displayed values update without the user reloading or re-requesting

#### Scenario: Available to a standard user
- **WHEN** a user whose bound user holds standard permission authenticates
- **THEN** the health view is available to them

### Requirement: The App Displays Only Values The Device Measured

The Web App SHALL display a device value only where the device reported it as a measurement. Where the device reports a value as unknown or not applicable, the app SHALL display that condition, and SHALL NOT display a number, a placeholder reading, or a value carried over from an earlier report as though it were current.

The app SHALL NOT display any configuration setting as a device measurement. In particular it SHALL NOT display the configured default battery percentage as the device's battery level.

#### Scenario: Unknown displayed as unknown
- **WHEN** the device reports a value as unknown
- **THEN** the app displays it as unknown
- **AND** the app displays no number for it

#### Scenario: Not applicable distinguished from unknown
- **WHEN** the device reports a subsystem as not applicable
- **THEN** the app's display of it differs from how it displays an unknown value

#### Scenario: Configuration is not displayed as measurement
- **WHEN** the app displays the device's battery level
- **THEN** the value shown came from the health report
- **AND** it did not come from the configured default battery percentage

#### Scenario: Stale value is not shown as current
- **WHEN** a previously reported value is no longer available
- **THEN** the app stops presenting the earlier value as the current one

### Requirement: Assumed Lock State Is Shown As Unconfirmed

Where the device reports a lock state it has assumed from a command rather than one the lock confirmed, the Web App SHALL show it as awaiting confirmation. It SHALL NOT present an assumed state identically to a confirmed one.

Where the device reports the age of a reading and that reading is old enough to be misleading, the app SHALL show how old it is.

#### Scenario: Assumed state marked
- **WHEN** the device reports a lock state marked as assumed
- **THEN** the app shows it as awaiting confirmation from the lock

#### Scenario: Confirmed state not marked
- **WHEN** the device reports a lock state confirmed by the lock
- **THEN** the app shows it without the awaiting-confirmation marking

#### Scenario: Old reading's age surfaced
- **WHEN** the device reports a reading old enough to be misleading
- **THEN** the app shows how old the reading is
