## ADDED Requirements

### Requirement: Zigbee Door Lock Cluster Server
The `sdf_protocol_zigbee` component SHALL implement a Zigbee ZHA Door Lock Cluster (0x0101) server endpoint.

#### Scenario: Cluster initialization
- **WHEN** `sdf_protocol_zigbee_init()` called
- **THEN** Register endpoint 1 with Door Lock Cluster server
- **THEN** Set attribute defaults: LockState=UNLOCKED, LockType=DEADBOLT, ActuatorEnabled=TRUE
- **THEN** Register command callbacks for Lock/Unlock/Latch/SetPINCode/ClearPINCode/etc.

### Requirement: Lock/Unlock Command Reception
The `sdf_protocol_zigbee` component SHALL receive and translate Zigbee lock commands to internal callbacks.

#### Scenario: Unlock command
- **WHEN** ZCL command `Unlock Door` (0x00) received on cluster 0x0101
- **THEN** Validate command (check permissions, not in lockout)
- **THEN** Invoke `command_cb(UNLOCK)` to `sdf_app`
- **THEN** Send Default Response (SUCCESS)

#### Scenario: Lock command
- **WHEN** ZCL command `Lock Door` (0x01) received
- **THEN** Invoke `command_cb(LOCK)`
- **THEN** Send Default Response

#### Scenario: Latch command
- **WHEN** ZCL command `Latch Door` (0x02) received
- **THEN** Invoke `command_cb(UNLATCH)`
- **THEN** Send Default Response

### Requirement: Programming Command Reception (Enrollment)
The `sdf_protocol_zigbee` component SHALL handle Zigbee programming commands for remote enrollment.

#### Scenario: Set PIN Code
- **WHEN** ZCL command `Set PIN Code` (0x08) received with user_id, pin, permission
- **THEN** Map PIN to fingerprint enrollment request
- **THEN** Invoke `command_cb(ENROLL, user_id, permission)`
- **THEN** Send Default Response

#### Scenario: Set RFID Code
- **WHEN** ZCL command `Set RFID Code` (0x12) received with user_id, rfid, permission
- **THEN** Map RFID to fingerprint enrollment request (same as PIN)
- **THEN** Invoke `command_cb(ENROLL, user_id, permission)`

#### Scenario: Clear PIN Code
- **WHEN** ZCL command `Clear PIN Code` (0x09) received with user_id
- **THEN** Invoke `command_cb(DELETE_USER, user_id)`
- **THEN** Send Default Response

#### Scenario: Clear All PIN Codes
- **WHEN** ZCL command `Clear All PIN Codes` (0x0A) received
- **THEN** Invoke `command_cb(CLEAR_ALL_USERS)`
- **THEN** Send Default Response

### Requirement: Attribute Reporting
The `sdf_protocol_zigbee` component SHALL report lock state, battery, alarm mask, and user list to Zigbee coordinator.

#### Scenario: Lock state reporting
- **WHEN** `sdf_protocol_zigbee_update_lock_state(state)` called
- **THEN** Update attribute 0x0000 (LockState): 0x00=NOT_FULLY_LOCKED, 0x01=LOCKED, 0x02=UNLOCKED
- **THEN** Send attribute report to coordinator (if bound) or on read

#### Scenario: Battery reporting
- **WHEN** `sdf_protocol_zigbee_update_battery(percentage)` called (periodic from `sdf_power`)
- **THEN** Update attribute 0x0031 (BatteryPercentage) 0-100
- **THEN** If < 20%: set alarm bit 0x0002 (LOW_BATTERY)

#### Scenario: Alarm mask reporting
- **WHEN** Security events occur
- **THEN** Update attribute 0x0032 (AlarmMask) with bits:
  - 0x0001: ACTION_FAILURE (BLE lock action failed)
  - 0x0002: LOW_BATTERY
  - 0x0004: BIOMETRIC_LOCKOUT
  - 0x0008: SECURITY_PROTOCOL (nonce replay)

#### Scenario: User list reporting (custom attribute)
- **WHEN** User enrolled/deleted
- **THEN** Update custom attribute 0x4000 (ActiveUsersList)
- **THEN** Format: JSON array `[{"id":1,"perm":3},{"id":5,"perm":1}]`
- **THEN** Send attribute report

### Requirement: Zigbee Network Management
The `sdf_protocol_zigbee` component SHALL support network steering (join) and factory reset.

#### Scenario: Network steering
- **WHEN** `sdf_protocol_zigbee_start_steering()` called (from admin action)
- **THEN** Enable permit join, start network steering
- **THEN** On join success: update state, report to app
- **THEN** On timeout (default 180s): stop steering

#### Scenario: Factory reset
- **WHEN** `sdf_protocol_zigbee_factory_reset()` called
- **THEN** Erase Zigbee persistent storage (NVRAM)
- **THEN** Leave network if joined
- **THEN** Reset to factory defaults

### Requirement: Sleepy End Device Behavior
The `sdf_protocol_zigbee` component SHALL operate as a Zigbee Sleepy End Device with configurable check-in interval.

#### Scenario: Check-in polling
- **WHEN** Deep sleep wakes for Zigbee check-in
- **THEN** Poll parent for pending messages (Data Request)
- **THEN** Process any received commands
- **THEN** Send attribute reports if changed
- **THEN** Return to sleep

#### Scenario: Stay awake for command processing
- **WHEN** Command received during check-in
- **THEN** Stay awake for `CONFIG_SDF_POWER_POST_WAKE_GUARD_MS` (default 1500ms)
- **THEN** Process command, send response
- **THEN** Return to sleep