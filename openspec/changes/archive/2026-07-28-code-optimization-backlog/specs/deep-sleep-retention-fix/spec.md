## ADDED Requirements

### Requirement: Deep sleep retention uses CRC-protected save function
The deep sleep path in `sdf_power_task()` SHALL use `sdf_power_save_retention()` instead of manually writing a retention struct without CRC.

#### Scenario: Entering deep sleep saves retention state with CRC
- **WHEN** `sdf_power_task()` decides to enter deep sleep
- **THEN** it populates `sdf_power_retention_t` with real state values (last_activity_us, next_checkin_us, etc.)
- **AND** calls `sdf_power_save_retention()` to compute CRC16 and write the struct to retention memory

#### Scenario: Retention state is populated from real system state
- **WHEN** `sdf_power_task()` prepares the retention struct before deep sleep
- **THEN** `last_activity_us` contains the current timer value
- **AND** `next_checkin_us` is set to current time plus checkin interval
- **AND** `ble_transport_state`, `zigbee_join_state`, `sensor_power_state`, `enrolled_user_count`, and `failed_attempts` are populated from their respective source modules or set to safe defaults

### Requirement: Prepare-deep-sleep function is either wired to real state or removed
The stub `sdf_power_prepare_deep_sleep()` SHALL either be wired to populate all fields from real state getters, or replaced by direct calls to `sdf_power_save_retention()`.

#### Scenario: All retention fields are populated with valid data
- **WHEN** `sdf_power_prepare_deep_sleep()` is called (if retained)
- **THEN** no field contains a hardcoded 0 value that should come from a live system module
- **AND** the CRC16 is valid for the full struct

#### Scenario: Deep sleep wake reads valid retention state
- **WHEN** the system wakes from deep sleep
- **THEN** `sdf_power_load_retention()` returns `ESP_OK` with a valid CRC
- **AND** `last_activity_us`, `next_checkin_us`, and other fields contain reasonable values from before sleep