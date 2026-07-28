# memory-guard Specification

## Purpose
Define the requirements for adding heap safety guards against memory allocation failures in hot paths within `sdf_services.c`. On ESP32-C6 with limited heap (~500KB after Wi-Fi/BLE/Zigbee stack), a failed `calloc()` during biometric unlock could crash the device.

## Requirements

### Requirement: Match cycle uses static buffers with no calloc failures
The system SHALL replace dynamic allocation in the match cycle with pre-allocated static buffers, ensuring no `calloc()` failures can occur during fingerprint matching.

#### Scenario: Match cycle completes without allocation
- **WHEN** `sdf_services_run_match_cycle()` is invoked
- **THEN** no dynamic memory allocation occurs during the match
- **AND** the match result is returned using only stack and static memory

#### Scenario: Match cycle handles high user count
- **WHEN** the system has up to 500 registered users
- **THEN** the match cycle still completes without `calloc()` failure
- **AND** static buffer size is sufficient for the maximum user count

### Requirement: Enrollment uses static buffers with no calloc failures
The system SHALL replace dynamic allocation in the enrollment path with pre-allocated static buffers, ensuring no `calloc()` failures during fingerprint enrollment.

#### Scenario: Enrollment completes without allocation
- **WHEN** `sdf_services_start_local_enrollment_with_permission()` is invoked
- **THEN** no dynamic memory allocation occurs for query buffers
- **AND** enrollment proceeds using only static and stack memory

#### Scenario: Enrollment under memory pressure
- **WHEN** heap is fragmented or low (below 4096 bytes free)
- **THEN** enrollment still completes without `calloc()` failure
- **AND** the static buffer is sufficient for the user query

### Requirement: Permission change uses static buffers with no calloc failures
The system SHALL replace dynamic allocation in the permission change path with pre-allocated static buffers, ensuring no `calloc()` failures during user permission updates.

#### Scenario: Permission change completes without allocation
- **WHEN** `sdf_services_change_user_permission()` is invoked
- **THEN** no dynamic memory allocation occurs for user lookup buffers
- **AND** permission change proceeds using only static and stack memory

#### Scenario: Permission change under memory pressure
- **WHEN** heap is fragmented or low (below 4096 bytes free)
- **THEN** permission change still completes without `calloc()` failure

### Requirement: OOM events are logged to audit trail
The system SHALL emit an audit event whenever an out-of-memory condition is detected, providing observability for operators.

#### Scenario: OOM detected during enrollment
- **WHEN** `esp_get_free_heap_size()` reports insufficient memory before enrollment allocation
- **THEN** an `SDF_AUDIT_PROTOCOL_ERROR` event is emitted with `ESP_ERR_NO_MEM`
- **AND** the enrollment path returns `ESP_ERR_NO_MEM` without crashing

#### Scenario: OOM detected during permission change
- **WHEN** `esp_get_free_heap_size()` reports insufficient memory before permission change allocation
- **THEN** an `SDF_AUDIT_PROTOCOL_ERROR` event is emitted with `ESP_ERR_NO_MEM`
- **AND** the permission change path returns `ESP_ERR_NO_MEM` without crashing

### Requirement: Static buffer sizes are sufficient for max user count
The system SHALL sized static buffers to accommodate the maximum user count of 500 without truncation.

#### Scenario: Query returns all users within buffer
- **WHEN** the system has 500 registered users
- **THEN** query results fit within the static buffer allocation
- **AND** no data is lost or truncated

### Requirement: Memory usage does not increase beyond static allocation overhead
The system SHALL not increase overall memory usage beyond the static allocation overhead (~2KB) introduced by the pre-allocated buffers.

#### Scenario: Baseline memory usage remains stable
- **WHEN** the system is under normal operation after applying memory guards
- **THEN** total heap usage is within 2KB of the pre-guard baseline
- **AND** no additional dynamic allocations occur in hot paths