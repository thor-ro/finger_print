## ADDED Requirements

### Requirement: Enrollment start delegates to init instead of duplicating reset logic
`sdf_enrollment_sm_start()` SHALL call `sdf_enrollment_sm_init()` to reset state, then set only the new fields (`state`, `user_id`, `permission`).

#### Scenario: Starting a new enrollment resets all state including retries
- **WHEN** `sdf_enrollment_sm_start()` is called
- **THEN** it calls `sdf_enrollment_sm_init(sm)` to reset all fields including retry counts
- **AND** sets `sm->state = SDF_ENROLLMENT_STATE_STEP_1`
- **AND** sets `sm->user_id = user_id`
- **AND** sets `sm->permission = permission`
- **AND** the returned `next` action is `SDF_ENROLL_ACT_EXECUTE_STEP` with cmd `SDF_FINGERPRINT_ENROLL_STEP_1`

### Requirement: Free-ID search iterates enrolled users instead of scanning all 4096 slots
`sdf_services_start_local_enrollment_with_permission()` SHALL find a free user ID by iterating through the enrolled user buffer instead of scanning user IDs 1..4096.

#### Scenario: Free ID found in enrolled user buffer
- **WHEN** searching for a free user ID with 5 enrolled users out of 4096 possible
- **THEN** the search iterates only through the 5 enrolled entries to find the first gap
- **AND** the search completes in O(5) time instead of O(4096)

#### Scenario: All user IDs in use returns error
- **WHEN** all user IDs from 1 to the maximum enrolled count are used
- **THEN** the search returns an error indicating no free IDs are available
- **AND** no unnecessary scan beyond the enrolled count is performed