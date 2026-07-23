## ADDED Requirements

### Requirement: Factory Reset Orchestration
The `factory-reset` capability SHALL provide a complete factory reset function that erases all persistent data and reboots the device into the unclaimed state (0 enrolled users).

#### Scenario: Complete factory reset via admin action
- **WHEN** Admin authorizes `SDF_SERVICES_ADMIN_ACTION_FACTORY_RESET` (8s button hold + admin fingerprint)
- **THEN** System erases all NVS data (Option B: full NVS erase)
- **THEN** System deletes all fingerprint templates from sensor
- **THEN** System erases Zigbee NVRAM (leaves network)
- **THEN** System resets all in-memory state to defaults
- **THEN** System reboots via `esp_restart()`
- **THEN** Device boots into UNCLAIMED state (LED breathes WHITE)

#### Scenario: Factory reset from CLI
- **WHEN** User enters `factory_reset` command in CLI
- **THEN** System prompts for confirmation
- **THEN** On confirmation, executes same sequence as admin action
- **THEN** Device reboots to unclaimed state

### Requirement: Unclaimed State Verification
The system SHALL verify the device is in unclaimed state after factory reset.

#### Scenario: Post-reset boot verification
- **WHEN** Device reboots after factory reset
- **THEN** `fp_get_user_count()` returns 0
- **THEN** `sdf_storage_nuki_load()` returns `ESP_ERR_NOT_FOUND`
- **THEN** `esp_zb_bdb_is_factory_new()` returns true
- **THEN** LED breathes WHITE continuously