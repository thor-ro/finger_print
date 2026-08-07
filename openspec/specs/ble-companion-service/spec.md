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
The system SHALL expose an OTA characteristic that accepts a chunked binary firmware transfer from an authenticated client over the existing BLE GATT connection. The firmware SHALL NOT establish any Wi-Fi or network connection to perform an OTA update, and SHALL stream the received bytes through the existing signed OTA verification flow.

A transfer SHALL begin with a control message declaring the total firmware image size, followed by one or more binary chunk writes sized to the connection's negotiated ATT MTU, and SHALL end with a control message that triggers integrity and signature verification of the fully-received image before commit. The system SHALL report per-chunk acknowledgement (confirmed byte offset) and final pass/fail verification status back to the client over the existing OTA notification path.

#### Scenario: OTA Triggered
- **WHEN** an authenticated client writes a begin-transfer message declaring the firmware image size to the OTA characteristic
- **THEN** system opens a new OTA session (`SDF_OTA_SOURCE_BLE`) sized to the declared image size
- **AND** system accepts subsequent chunk writes, appending each to the OTA session and acknowledging the confirmed byte offset

#### Scenario: OTA transfer completed and verified
- **WHEN** an authenticated client writes an end-transfer message after sending exactly the declared number of bytes
- **THEN** system verifies the accumulated image's integrity and Ed25519 signature
- **AND** system commits and reports success only if verification passes, or reports failure and does not commit if verification fails

#### Scenario: Malformed or oversized OTA request rejected
- **WHEN** a client writes a malformed begin-transfer message, an oversized declared image size, or chunk data exceeding the negotiated MTU
- **THEN** system rejects the write
- **AND** system does not open or continue an OTA session

#### Scenario: BLE disconnect during transfer allows resume
- **WHEN** an authenticated client that was mid-transfer reconnects and re-authenticates before the in-progress OTA session times out
- **THEN** system reports the currently confirmed byte offset
- **AND** system accepts further chunk writes continuing from that offset without restarting the transfer

#### Scenario: Abandoned OTA session times out
- **WHEN** no chunk write is received for an in-progress OTA session within the configured idle timeout
- **THEN** system aborts the OTA session
- **AND** system releases the update partition so a subsequent OTA attempt can begin cleanly

### Requirement: Persisted Notification Subscription Capacity
The system SHALL size its persisted notification-subscription (CCCD) storage capacity to cover the worst case of every bonded peer subscribing to every NOTIFY-capable characteristic exposed by the Companion Service. The configured CCCD capacity SHALL be greater than or equal to the product of the maximum number of bonded peers and the number of NOTIFY-capable characteristics.

#### Scenario: All bonded peers subscribed to all notify characteristics
- **WHEN** the maximum number of bonded peers are each subscribed to every NOTIFY-capable characteristic
- **THEN** every subscription persists successfully across reconnects
- **AND** no persisted subscription is silently dropped due to exhausted CCCD storage capacity

#### Scenario: Adding a NOTIFY-capable characteristic requires capacity review
- **WHEN** a new NOTIFY-capable characteristic is added to the Companion Service's GATT database
- **THEN** the persisted CCCD capacity SHALL be re-verified to still cover the updated bonded-peer × NOTIFY-characteristic product
