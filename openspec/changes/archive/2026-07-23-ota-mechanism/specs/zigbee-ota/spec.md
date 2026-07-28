## ADDED Requirements

### Requirement: Zigbee OTA Integration with sdf_ota
The existing Zigbee OTA handler in sdf_protocol_zigbee SHALL delegate to sdf_ota component for session management, version check, and signature verification.

#### Scenario: Zigbee OTA start delegates to sdf_ota
- **WHEN** ESP_ZB_ZCL_OTA_UPGRADE_STATUS_START received
- **THEN** sdf_ota_begin() called with image size, returns handle for subsequent writes

#### Scenario: Zigbee OTA data write delegates to sdf_ota
- **WHEN** ESP_ZB_ZCL_OTA_UPGRADE_STATUS_RECEIVE received with payload
- **THEN** sdf_ota_write() called with chunk data, returns write status

#### Scenario: Zigbee OTA apply delegates to sdf_ota
- **WHEN** ESP_ZB_ZCL_OTA_UPGRADE_STATUS_APPLY received
- **THEN** sdf_ota_verify_and_commit() called, performs version check + signature verify + commit

#### Scenario: Zigbee OTA check delegates to sdf_ota
- **WHEN** ESP_ZB_ZCL_OTA_UPGRADE_STATUS_CHECK received
- **THEN** sdf_ota_verify_integrity() called, returns pass/fail for size match

#### Scenario: Zigbee OTA finish triggers reboot
- **WHEN** ESP_ZB_ZCL_OTA_UPGRADE_STATUS_FINISH received and commit succeeded
- **THEN** esp_restart() called after setting boot partition

### Requirement: Manufacturer Code and Image Type
The Zigbee OTA cluster SHALL use manufacturer code 0x1011 and image type 0x1111 (matching existing config).

#### Scenario: OTA cluster attributes
- **WHEN** Zigbee OTA cluster queried for manufacturer/image type
- **THEN** Returns 0x1011 / 0x1111

### Requirement: Query Interval Configurable via Kconfig
The Zigbee OTA client SHALL support configurable query interval via Kconfig option `CONFIG_SDF_OTA_ZIGBEE_QUERY_INTERVAL_HOURS` (default 24h).

#### Scenario: Query interval from Kconfig
- **WHEN** sdf_ota_init() called
- **THEN** esp_zb_ota_upgrade_client_query_interval_set() called with value from Kconfig

### Requirement: OTA Progress Reporting to Coordinator
During Zigbee OTA download, the system SHALL report progress to the Zigbee coordinator using ESP Zigbee OTA cluster progress reporting.

#### Scenario: Progress report on each chunk
- **WHEN** sdf_ota_write() called with ZIGBEE source and chunk data
- **THEN** esp_zb_ota_upgrade_client_progress_report() called with current offset and total image size

#### Scenario: Progress report at apply
- **WHEN** sdf_ota_verify_and_commit() called for ZIGBEE source
- **THEN** Final progress report sent (100%) before commit

## REMOVED Requirements

### Requirement: Inline OTA Logic in Zigbee Handler
**Reason**: OTA logic moved to dedicated sdf_ota component for reusability across Zigbee, CLI, and future BLE triggers
**Migration**: sdf_protocol_zigbee now calls sdf_ota API instead of inline esp_ota_* calls