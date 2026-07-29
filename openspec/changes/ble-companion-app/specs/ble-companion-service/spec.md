## ADDED Requirements

### Requirement: BLE GATT Authentication
The system SHALL expose an Authentication characteristic. Until valid credentials (username and password hash) are written to this characteristic, all other restricted characteristics (Config, Enrollment, OTA) SHALL return insufficient authentication errors.

#### Scenario: Unauthorized access blocked
- **WHEN** unauthenticated client attempts to write to the Config characteristic
- **THEN** system returns ESP_GATT_AUTH_FAIL

#### Scenario: Successful authentication
- **WHEN** client writes valid username and password hash to Auth characteristic
- **THEN** system transitions BLE connection to authenticated state
- **AND** subsequent writes to Config characteristic succeed

### Requirement: OTA Triggering via BLE
The system SHALL expose an OTA characteristic that accepts Wi-Fi credentials. Upon receiving valid credentials, the system SHALL initiate a Wi-Fi connection and begin the OTA firmware download process.

#### Scenario: OTA Triggered
- **WHEN** client writes Wi-Fi SSID and Password to OTA characteristic
- **THEN** system initiates Wi-Fi connection and begins firmware download
