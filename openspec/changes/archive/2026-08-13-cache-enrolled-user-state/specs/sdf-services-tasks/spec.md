## MODIFIED Requirements

### Requirement: User Query Buffer Sizing
The `sdf_services` component SHALL use static buffers sized for the maximum supported user count (10) rather than the sensor's hardware maximum (4095). Buffers SHALL use a compact bitmap + packed permissions representation to minimize RAM usage. This representation SHALL be persisted in `sdf_services_state_t` as the authoritative enrolled-user record; `sdf_services_query_users()` SHALL be served from this cached representation and SHALL NOT issue a sensor query.

#### Scenario: Query users for Zigbee sync
- **WHEN** `sdf_app_update_zigbee_user_list()` is called
- **THEN** the cached bitmap + packed permissions are read directly, with no sensor UART round trip
- **AND** user list is serialized to Zigbee attribute 0x4000

#### Scenario: Query users for local enrollment
- **WHEN** `sdf_services_start_local_enrollment_with_permission()` queries existing users
- **THEN** the cached bitmap of 16 bits tracking occupied IDs 1-10 is read directly, with no sensor UART round trip
- **AND** permissions are read from the packed 2-bit-per-user array

#### Scenario: Query users for permission change
- **WHEN** `sdf_services_change_user_permission()` queries existing users
- **THEN** the cached bitmap and packed permissions are read directly, with no sensor UART round trip
- **AND** admin count is computed from the cached packed permissions

## ADDED Requirements

### Requirement: Enrolled-User Cache Is Authoritative From Boot
The enrolled-user bitmap and packed permissions SHALL be persisted to NVS and loaded into `sdf_services_state_t` synchronously during `sdf_services_init()`, before any task that reads enrolled-user state is started. `enrolled_user_count` SHALL be computed as the population count of the cached bitmap rather than stored as an independently maintained field. No boot-time sensor query SHALL be required to determine enrolled-user state.

#### Scenario: Admin gate correct immediately after boot
- **WHEN** the device powers on with one or more users already enrolled
- **THEN** the enrolled-user count read by any task, including the very first button press after boot, reflects the persisted enrolled users
- **AND** admin-gated actions are never executed without fingerprint authorization due to a boot-time race

#### Scenario: Unclaimed device boots with zero users
- **WHEN** the device powers on with no persisted enrolled users
- **THEN** the enrolled-user count reads 0 immediately
- **AND** actions that require no prior admin (e.g. first enrollment) proceed without a fingerprint gate, consistent with unclaimed-device behavior

### Requirement: Synchronous NVS Persistence On Enrollment Mutation
Every successful change to enrolled-user state (enroll, delete, clear-all, permission change) SHALL update the in-memory cache and persist it to NVS before the operation is reported as successful to its caller.

#### Scenario: Enrollment persists before success is reported
- **WHEN** a fingerprint enrollment completes successfully on the sensor
- **THEN** the cache is updated and written to NVS
- **AND** enrollment is only reported complete to the caller after the NVS write succeeds

#### Scenario: Delete persists before success is reported
- **WHEN** a user is deleted from the sensor successfully
- **THEN** the cache is updated and written to NVS
- **AND** deletion is only reported complete to the caller after the NVS write succeeds

### Requirement: NVS Write Failure Handling
If the NVS write fails after a successful enrollment, the system SHALL retry the write with backoff; if retries are exhausted, the system SHALL roll back the sensor-side enrollment (delete the newly added print) and report the enrollment as failed, so the sensor and the persisted cache never disagree. If the NVS write fails after a successful delete, clear-all, or permission change, the system SHALL retry the write with backoff and report failure to the caller if retries are exhausted, without attempting a sensor-side rollback. In both cases, exhausting retries SHALL trigger the red LED error indication.

#### Scenario: Enrollment NVS write fails after retries exhausted
- **WHEN** an enrollment succeeds on the sensor but the NVS write fails on every retry
- **THEN** the newly enrolled print is deleted from the sensor
- **AND** enrollment is reported as failed to the caller
- **AND** the LED flashes red

#### Scenario: Delete NVS write fails after retries exhausted
- **WHEN** a user deletion succeeds on the sensor but the NVS write fails on every retry
- **THEN** deletion is reported as failed to the caller
- **AND** the LED flashes red
