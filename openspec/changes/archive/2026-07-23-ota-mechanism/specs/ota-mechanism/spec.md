## ADDED Requirements

### Requirement: OTA Update Trigger
The system SHALL provide three independent mechanisms to trigger an OTA update:
1. Zigbee OTA cluster command from coordinator
2. CLI command `ota trigger <source>`
3. Future BLE Peripheral OTA service (architecture only)

#### Scenario: Zigbee OTA trigger
- **WHEN** Zigbee coordinator sends OTA upgrade start command
- **THEN** sdf_ota initializes OTA session, validates image size against partition, begins write

#### Scenario: CLI OTA trigger
- **WHEN** Authenticated user runs `ota trigger zigbee://` or `ota trigger <url>`
- **THEN** sdf_ota initiates OTA session from specified source

### Requirement: OTA Session Management
The system SHALL manage OTA session state through: IDLE → DOWNLOADING → VERIFYING → COMMITTING → COMPLETE/FAILED

#### Scenario: Successful OTA flow
- **WHEN** OTA session starts and image writes complete without error
- **THEN** system verifies signature, checks version, commits new partition, schedules reboot

#### Scenario: Failed OTA flow
- **WHEN** Signature verification fails or version check fails
- **THEN** system aborts session, marks target partition invalid, remains on current firmware

### Requirement: Partition Management
The system SHALL use ESP-IDF OTA APIs (esp_ota_begin, esp_ota_write, esp_ota_end, esp_ota_set_boot_partition) for all partition operations.

#### Scenario: Write to inactive partition
- **WHEN** OTA session writes data chunks
- **THEN** data is written to partition returned by esp_ota_get_next_update_partition()

#### Scenario: Commit on success
- **WHEN** Verification passes
- **THEN** esp_ota_set_boot_partition() called on target partition, device reboots

### Requirement: Rollback Capability
The system SHALL support both automatic rollback on boot failure and manual rollback via CLI.

#### Scenario: Automatic rollback on boot failure
- **WHEN** New firmware fails to boot (watchdog timeout, crash loop) and bootloader rollback enabled
- **THEN** Bootloader marks app invalid, rolls back to previous partition, boots previous firmware

#### Scenario: Manual rollback via CLI
- **WHEN** Authenticated user runs `ota rollback`
- **THEN** System calls esp_ota_mark_app_invalid_rollback_and_reboot(), device reboots into previous firmware

### Requirement: Version Check Before Commit
The system SHALL verify incoming firmware version before committing. Downgrades SHALL be allowed with warning logged.

#### Scenario: Version upgrade
- **WHEN** Incoming version > current version
- **THEN** Commit proceeds, version transition logged as upgrade

#### Scenario: Version downgrade
- **WHEN** Incoming version < current version
- **THEN** Commit proceeds, warning logged, version transition logged as downgrade

#### Scenario: Same version
- **WHEN** Incoming version == current version
- **THEN** Commit proceeds, logged as reinstall

### Requirement: Audit Trail
The system SHALL log all OTA events (trigger, start, verify, commit, rollback, failure) with timestamp, source, version, and result to audit callback.

#### Scenario: OTA audit events
- **WHEN** Any OTA state transition occurs
- **THEN** Audit event emitted via sdf_app audit callback with type OTA_*