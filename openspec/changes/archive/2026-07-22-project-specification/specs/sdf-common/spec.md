## ADDED Requirements

### Requirement: Shared Type Definitions
The `sdf_common` component SHALL define all shared types used across components.

#### Scenario: Lock action enum
- **WHEN** `sdf_lock_action_t` used
- **THEN** Values: `LOCK=0`, `UNLOCK=1`, `UNLATCH=2`, `LOCK_N_GO=3`

#### Scenario: Keyturner state enum
- **WHEN** `sdf_keyturner_state_t` used
- **THEN** Values: `UNKNOWN=0`, `LOCKED=1`, `UNLOCKED=2`, `UNLATCHED=3`, `MOTOR_BLOCKED=4`, `UNDEFINED=5`

#### Scenario: User permission enum
- **WHEN** `sdf_user_permission_t` used
- **THEN** Values: `NONE=0`, `STANDARD=1`, `ELEVATED=2`, `ADMIN=3`

#### Scenario: Event/audit struct
- **WHEN** `sdf_audit_event_t` used
- **THEN** Fields: `event_type (uint16_t)`, `timestamp_us (uint64_t)`, `user_id (uint16_t)`, `data (uint32_t)`

#### Scenario: Error codes
- **WHEN** `sdf_err_t` used
- **THEN** Values: `OK=0`, `ERR_BASE=-0x1000`, `ERR_TIMEOUT=-0x1001`, `ERR_NOT_FOUND=-0x1002`, `ERR_INVALID_ARG=-0x1003`, `ERR_NO_MEM=-0x1004`, `ERR_BUSY=-0x1005`, `ERR_CRC=-0x1006`, `ERR_PROTOCOL=-0x1007`, `ERR_CRYPTO=-0x1008`, `ERR_NOT_PAIRED=-0x1009`, `ERR_NONCE_REUSE=-0x100A`

### Requirement: Lock Guard (Mutex Wrapper)
The `sdf_common` component SHALL provide a lock guard macro for mutex-protected critical sections.

#### Scenario: Lock guard usage
- **WHEN** `SDF_LOCK_GUARD(mutex)` used in scope
- **THEN** Lock mutex on entry, unlock on exit (including early return)
- **THEN** Uses `__attribute__((cleanup))` for RAII-style cleanup

### Requirement: Mock Interface Definitions
The `sdf_common` component SHALL define mock function pointer interfaces for Linux host testing.

#### Scenario: Mock fingerprint interface
- **WHEN** `sdf_mock_fp_t` struct used
- **THEN** Function pointers: `match_1n`, `enroll_step`, `delete_user`, `delete_all_users`, `get_user_count`, `get_user_list`, `control_led`

#### Scenario: Mock LED interface
- **WHEN** `sdf_mock_led_t` struct used
- **THEN** Function pointers: `flash_green`, `flash_red`, `pulse_blue`, `solid_green`, `breathe_white`, `flash_yellow_fast`, `pulse_cyan`, `pulse_red_slow`, `admin_auth_green`

#### Scenario: Mock BLE interface
- **WHEN** `sdf_mock_ble_t` struct used
- **THEN** Function pointers: `enable`, `disable`, `start_pairing`, `send_lock_action`, `is_paired`

#### Scenario: Mock Zigbee interface
- **WHEN** `sdf_mock_zb_t` struct used
- **THEN** Function pointers: `update_lock_state`, `update_battery`, `update_alarm_mask`, `update_user_list`

#### Scenario: Mock storage interface
- **WHEN** `sdf_mock_storage_t` struct used
- **THEN** Function pointers: `get/set/erase nuki credentials`, `get/set/erase target addr`, `get/set/erase security`, `erase_all`

### Requirement: Utility Functions
The `sdf_common` component SHALL provide common utilities.

#### Scenario: Byte order
- **WHEN** `sdf_le16_to_cpu/le32_to_cpu/cpu_to_le16/cpu_to_le32` used
- **THEN** Convert little-endian to/from host byte order

#### Scenario: CRC16
- **WHEN** `sdf_crc16_ccitt(data, len)` called
- **THEN** Compute CRC16-CCITT (poly 0x1021, init 0xFFFF)

#### Scenario: Hex dump
- **WHEN** `sdf_hex_dump(buf, len)` called
- **THEN** Print formatted hex dump to log