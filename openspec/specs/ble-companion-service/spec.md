# Spec: ble-companion-service

## Overview
This specification covers the BLE Companion Service, which acts as a GATT server on the shared NimBLE host to enable the Web Companion App to connect, authenticate, manage configuration, and trigger OTA updates.

## Requirements

### Requirement: Shared NimBLE Lifecycle
The Companion Service SHALL register its GATT database with the existing NimBLE host before that host starts. `sdf_protocol_ble` SHALL be the sole owner of NimBLE initialization, host-task creation, and host lifecycle callbacks.

#### Scenario: Companion and Nuki roles start together
- **WHEN** the application initializes BLE
- **THEN** the Companion Service registers before the shared NimBLE host starts
- **AND** the Nuki client and Companion Service operate as central and peripheral roles on that single host

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
The system SHALL expose an OTA characteristic that accepts a bounded UTF-8 JSON object with `ssid`, `password`, and `firmwareUrl` fields from an authenticated client. The firmware SHALL require `firmwareUrl` to use HTTPS, use the Wi-Fi credentials only for the OTA attempt, and stream the download through the existing signed OTA verification flow.

#### Scenario: OTA Triggered
- **WHEN** an authenticated client writes valid Wi-Fi credentials and an HTTPS firmware URL to OTA characteristic
- **THEN** system initiates a Wi-Fi connection and begins the verified firmware download

#### Scenario: Malformed or insecure OTA request rejected
- **WHEN** a client writes malformed, oversized, or non-HTTPS OTA request data
- **THEN** system rejects the write
- **AND** system does not initiate Wi-Fi or an OTA session
