# Spec: web-companion-app

## Purpose
This specification covers the Web Companion App, a static web application that connects to the Smart Door Bridge via Web Bluetooth to allow users to register, manage device configuration, and trigger OTA firmware updates.

## Requirements

### Requirement: Web Bluetooth Connection
The Web App SHALL use the Web Bluetooth API (WebBLE) to scan for and connect to the Smart Door Bridge's Companion Service.

#### Scenario: Device Discovery
- **WHEN** user clicks "Connect" in the web app
- **THEN** browser prompts with Web Bluetooth device picker filtering for the Smart Door service UUID

### Requirement: Static App Location
The Web App SHALL be maintained as dependency-free static assets in the repository's `web-companion/` directory, suitable for GitHub Pages deployment.

#### Scenario: GitHub Pages deployment assets are present
- **WHEN** the repository is prepared for deployment
- **THEN** `web-companion/` contains the HTML, client-side assets, and deployment instructions needed to host the app

### Requirement: First-Time Setup Wizard
The Web App SHALL provide a first-time setup wizard that guides the user through claiming an unclaimed device. The wizard SHALL be the only supported path for first-time setup; the device exposes no physical alternative for enrolling the first Admin or pairing the Nuki lock.

The wizard SHALL read the device's setup state before login and resume the user at the step that state implies, so that a user who reconnects mid-setup is not asked to repeat completed steps within the same setup phase.

#### Scenario: Wizard entered on an unclaimed device
- **WHEN** the app connects to a device reporting that setup has not started
- **THEN** the app presents the setup wizard rather than the login form

#### Scenario: Wizard skipped on a completed device
- **WHEN** the app connects to a device reporting setup complete
- **THEN** the app presents the ordinary login form

#### Scenario: Wizard resumes at the reported step
- **WHEN** the app connects to a device in the setup phase reporting that a companion account is registered but Nuki is not paired
- **THEN** the wizard resumes at the Nuki pairing step

#### Scenario: Wizard resumes at registration when no account exists
- **WHEN** the app connects to a device in the setup phase reporting that an Admin is enrolled but no companion account has been registered
- **THEN** the wizard resumes at the account registration step
- **AND** it does not skip ahead to Nuki pairing

### Requirement: Wizard Step Order
The wizard SHALL guide the user through Admin fingerprint enrolment first, then companion account registration, then Nuki pairing, then explicit completion. Account registration SHALL NOT be offered before an Admin fingerprint has been enrolled, because registration is authorized by an Admin fingerprint scan that would otherwise have nothing to match.

#### Scenario: Admin enrolment precedes registration
- **WHEN** the wizard starts on a device with no enrolled users
- **THEN** the first step presented is Admin fingerprint enrolment
- **AND** the registration form is not reachable until enrolment succeeds

#### Scenario: Registration authorized by the newly enrolled admin
- **WHEN** the user submits the registration form during the wizard
- **THEN** the app prompts the user to scan the Admin finger on the device to confirm
- **AND** the scan is matched against the Admin enrolled in the previous step

#### Scenario: Nuki pairing guided with physical instructions
- **WHEN** the wizard reaches the Nuki pairing step
- **THEN** the app instructs the user to put the Nuki lock into pairing mode before proceeding
- **AND** reports the pairing outcome returned by the device

### Requirement: User Registration
The Web App SHALL provide a registration form that hashes the user's password locally (e.g., using SHA256) before sending it over BLE to the bridge. The bridge is responsible for salting and stretching the received hash before persisting it; the Web App does not need to know or apply the bridge's key-derivation parameters at registration time.

During first-time setup, registration SHALL be offered only after an Admin fingerprint has been enrolled, since the bridge authorizes registration with an Admin fingerprint scan.

#### Scenario: Secure Registration
- **WHEN** user submits the registration form
- **THEN** web app computes SHA256 hash of password
- **AND** transmits username and hash to the bridge
- **AND** prompts the user to "Please scan the Admin Finger on the device to confirm."

#### Scenario: Registration unavailable before an admin exists
- **WHEN** the app is connected to a device in the setup phase with no enrolled users
- **THEN** the registration form is not offered

### Requirement: User Login
The Web App SHALL log a user in using a two-step challenge-response exchange rather than sending a static password hash. The app SHALL first request a login challenge for the entered username, then locally derive a stretched credential from the password using the key-derivation parameters (salt and iteration count) returned in that challenge, and finally send a response computed from that stretched credential and the challenge's nonce.

#### Scenario: Login challenge requested
- **WHEN** user submits the login form
- **THEN** web app sends the entered username to the bridge to request a login challenge
- **AND** app receives a salt, an iteration count, and a nonce in response

#### Scenario: Login response computed and sent
- **WHEN** web app has received a login challenge
- **THEN** app derives a stretched credential from the entered password using the received salt and iteration count
- **AND** app computes a response value from the stretched credential and the received nonce
- **AND** app sends that response value to the bridge to complete login

#### Scenario: Successful login
- **WHEN** the bridge accepts the app's login response
- **THEN** app transitions to the authenticated state and enables access to Config, Enrollment, and OTA features

#### Scenario: Failed login
- **WHEN** the bridge rejects the app's login response
- **THEN** app reports a login failure to the user without revealing whether the entered username exists

### Requirement: OTA Battery Warning
The Web App SHALL warn the user before triggering an OTA update about potential battery drain and SHALL stream the selected firmware image as a chunked binary transfer over the OTA characteristic using the begin/chunk/end opcode contract.

#### Scenario: OTA Warning
- **WHEN** user selects a firmware file and starts an OTA update
- **THEN** app displays a warning: "Ensure your battery is above 20%. OTA transfer over Bluetooth draws significant power and firmware is large — keep the app open and the device nearby until it completes."
- **AND** app writes a begin-transfer message (`0x01`, 4-byte little-endian image size) to the OTA characteristic after user confirmation

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

### Requirement: Explicit Setup Completion
The Web App SHALL complete setup by issuing an explicit completion request over its authenticated session, and SHALL inform the user that completing setup locks the device to the currently connected companion device.

#### Scenario: User completes setup
- **WHEN** the user confirms the final wizard step
- **THEN** the app issues the setup-completion request
- **AND** on success reports that the device is now claimed and paired to this browser

#### Scenario: Completion rejected
- **WHEN** the device rejects the completion request because prerequisites are unmet
- **THEN** the app reports which step is still outstanding and returns the user to it

#### Scenario: Completion fails on a device fault
- **WHEN** the device rejects the completion request because of an internal fault rather than an unmet prerequisite
- **THEN** the app reports a device error and offers to retry completion
- **AND** it does not send the user back to redo a step that already succeeded

### Requirement: Setup Timeout Is Communicated
The Web App SHALL inform the user that the setup phase is time-bounded and that a lapse discards all progress, and SHALL report a lapse when the connection is lost to a setup timeout.

#### Scenario: Timeout is disclosed before setup starts
- **WHEN** the wizard is presented on an unclaimed device
- **THEN** the app states that setup must be completed within the device's setup window and that a lapse erases all progress

#### Scenario: Lapsed setup is reported
- **WHEN** the device disconnects and stops advertising without setup having completed
- **THEN** the app reports that the setup window elapsed, that progress was discarded, and that the user must press the device button to start again
