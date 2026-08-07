## MODIFIED Requirements

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
