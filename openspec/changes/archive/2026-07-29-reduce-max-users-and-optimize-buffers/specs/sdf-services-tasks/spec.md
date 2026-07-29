## Purpose

Add user capacity limits and optimized buffer management to the sdf-services-tasks capability. Reduce max users from 4095 to 10 and implement bitmap + packed permissions for ~99.6% RAM savings on static buffers.

## ADDED Requirements

### Requirement: User Capacity
The fingerprint sensor SHALL support User IDs from `1` to `10`. When a new user is enrolled locally, the firmware SHALL automatically assign the lowest available User ID. If all User IDs are occupied, the LED SHALL flash **red** and enrollment SHALL be rejected.

#### Scenario: Enroll user within capacity
- **WHEN** device has < 10 enrolled users and enrollment is initiated
- **THEN** user is enrolled with lowest available User ID (1-10)
- **AND** LED flashes **green** on each successful scan
- **AND** LED shows solid **green** on completion

#### Scenario: Enroll user at capacity
- **WHEN** device has 10 enrolled users and enrollment is initiated
- **THEN** enrollment is rejected
- **AND** LED flashes **red**

#### Scenario: Automatic User ID assignment finds gaps
- **WHEN** users 1, 2, 4, 5 are enrolled (user 3 was deleted)
- **THEN** next enrollment assigns User ID 3 (lowest available)

### Requirement: User Query Buffer Sizing
The `sdf_services` component SHALL use static buffers sized for the maximum supported user count (10) rather than the sensor's hardware maximum (4095). Buffers SHALL use a compact bitmap + packed permissions representation to minimize RAM usage.

#### Scenario: Query users for Zigbee sync
- **WHEN** `sdf_app_update_zigbee_user_list()` is called
- **THEN** static buffers of 10 entries are used (no dynamic allocation)
- **AND** user list is serialized to Zigbee attribute 0x4000

#### Scenario: Query users for local enrollment
- **WHEN** `sdf_services_start_local_enrollment_with_permission()` queries existing users
- **THEN** static buffer of 10 entries is used
- **AND** bitmap of 16 bits tracks occupied IDs 1-10
- **AND** permissions packed as 2-bit values in uint8_t array

#### Scenario: Query users for permission change
- **WHEN** `sdf_services_change_user_permission()` queries existing users
- **THEN** static buffer of 10 entries is used
- **AND** admin count is computed from packed permissions

### Requirement: Static RAM Buffer Optimization (Optimization #16)
The system SHALL implement the documented optimization #16: replace 3072-byte static buffers with bitmap + packed permissions representation achieving ~99.6% RAM savings.

#### Scenario: Enrollment query buffer
- **WHEN** `sdf_services_start_local_enrollment_with_permission()` runs
- **THEN** uses `uint16_t user_ids[10]` + `uint8_t permissions[10]` = 30 bytes
- **AND** bitmap `uint16_t occupied_ids = 0` (1 bit per ID 1-10)
- **AND** packed permissions `uint8_t packed_perms[4]` (2 bits per user)

#### Scenario: Permission change query buffer
- **WHEN** `sdf_services_change_user_permission()` runs
- **THEN** uses same compact representation
- **AND** admin count computed by iterating packed 2-bit permissions

### Requirement: CLI User ID Validation
The CLI commands SHALL validate User IDs against the new maximum of 10.

#### Scenario: User add with valid ID
- **WHEN** `user add 5 1` is executed
- **THEN** command accepts ID 5 (within 1-10 range)

#### Scenario: User add with invalid ID
- **WHEN** `user add 11 1` is executed
- **THEN** command rejects with "Invalid user_id. Expected 1-10."

#### Scenario: User get with invalid ID
- **WHEN** `user get 15` is executed
- **THEN** command rejects with "Invalid user_id. Expected 1-10."

#### Scenario: User del with invalid ID
- **WHEN** `user del 20` is executed
- **THEN** command rejects with "Invalid user_id. Expected 1-10."

#### Scenario: User permission with invalid ID
- **WHEN** `user permission 12 3` is executed
- **THEN** command rejects with "Invalid user_id. Expected 1-10."