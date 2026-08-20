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

### Requirement: User Registration
The Web App SHALL provide a registration form that hashes the user's password locally (e.g., using SHA256) before sending it over BLE to the bridge. The bridge is responsible for salting and stretching the received hash before persisting it; the Web App does not need to know or apply the bridge's key-derivation parameters at registration time.

#### Scenario: Secure Registration
- **WHEN** user submits the registration form
- **THEN** web app computes SHA256 hash of password
- **AND** transmits username and hash to the bridge
- **AND** prompts the user to "Please scan the Admin Finger on the device to confirm."

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
