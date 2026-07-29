## ADDED Requirements

### Requirement: Web Bluetooth Connection
The Web App SHALL use the Web Bluetooth API (WebBLE) to scan for and connect to the Smart Door Bridge's Companion Service.

#### Scenario: Device Discovery
- **WHEN** user clicks "Connect" in the web app
- **THEN** browser prompts with Web Bluetooth device picker filtering for the Smart Door service UUID

### Requirement: User Registration
The Web App SHALL provide a registration form that hashes the user's password locally (e.g., using SHA256) before sending it over BLE to the bridge.

#### Scenario: Secure Registration
- **WHEN** user submits the registration form
- **THEN** web app computes SHA256 hash of password
- **AND** transmits username and hash to the bridge
- **AND** prompts the user to "Please scan the Admin Finger on the device to confirm."

### Requirement: OTA Battery Warning
The Web App SHALL warn the user before triggering an OTA update about potential battery drain.

#### Scenario: OTA Warning
- **WHEN** user selects an OTA update to install
- **THEN** app displays a warning: "Ensure your battery is above 20%. OTA uses Wi-Fi and draws significant power."
