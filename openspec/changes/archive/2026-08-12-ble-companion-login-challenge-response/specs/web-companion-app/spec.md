## MODIFIED Requirements

### Requirement: User Registration
The Web App SHALL provide a registration form that hashes the user's password locally (e.g., using SHA256) before sending it over BLE to the bridge. The bridge is responsible for salting and stretching the received hash before persisting it; the Web App does not need to know or apply the bridge's key-derivation parameters at registration time.

#### Scenario: Secure Registration
- **WHEN** user submits the registration form
- **THEN** web app computes SHA256 hash of password
- **AND** transmits username and hash to the bridge
- **AND** prompts the user to "Please scan the Admin Finger on the device to confirm."

## ADDED Requirements

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
