# Spec: Enrollment State Machine Integration

## Scope
Complete the integration of the enhanced enrollment state machine into sdf_services, ensuring the executor properly uses the `sdf_enrollment_sm_apply_step_result_ex()` API with full action routing.

## Requirements

### Requirement: Executor uses enhanced SM API
The `sdf_services_run_enrollment_step()` function SHALL use `sdf_enrollment_sm_apply_step_result_ex()` to get the next action instead of the legacy `sdf_enrollment_sm_apply_step_result()`.

#### Scenario: Enhanced API returns EXECUTE_STEP
- **WHEN** enrollment state machine is in STEP_1 and receives OP_OK
- **THEN** it returns `SDF_ENROLL_ACT_EXECUTE_STEP` with cmd=STEP_2
- **WHEN** enrollment state machine is in STEP_2 and receives OP_OK
- **THEN** it returns `SDF_ENROLL_ACT_EXECUTE_STEP` with cmd=STEP_3
- **WHEN** enrollment state machine is in STEP_3 and receives OP_OK
- **THEN** it returns `SDF_ENROLL_ACT_COMPLETE`

### Requirement: ACK_FAIL triggers RETRY_STEP
The enrollment state machine SHALL return `SDF_ENROLL_ACT_RETRY_STEP` when ACK_FAIL occurs on steps 1 or 2, provided retry count has not exceeded max.

#### Scenario: RETRY_STEP on step 1 ACK_FAIL
- **WHEN** enrollment state machine is in STEP_1 and receives SDF_FINGERPRINT_OP_FAILED
- **THEN** it returns `SDF_ENROLL_ACT_RETRY_STEP` with same cmd and retry_count=1
- **WHEN** retry_count reaches max_retries_step1 (3) and FAIL occurs
- **THEN** it returns `SDF_ENROLL_ACT_FAIL`

### Requirement: Step 3 ACK_FAIL fails immediately
The enrollment state machine SHALL return `SDF_ENROLL_ACT_FAIL` immediately on any failure at step 3, without retrying.

#### Scenario: Step 3 ACK_FAIL no retry
- **WHEN** enrollment state machine is in STEP_3 and receives SDF_FINGERPRINT_OP_FAILED
- **THEN** it returns `SDF_ENROLL_ACT_FAIL` immediately

### Requirement: Events emitted with correct payloads
The enrollment task SHALL emit `ENROLLMENT_COMPLETE` with user_id and permission, and `ENROLLMENT_FAILED` with step and error_code.

#### Scenario: Complete event payload
- **WHEN** enrollment completes successfully
- **THEN** `ENROLLMENT_COMPLETE` event includes user_id and permission from enrollment

#### Scenario: Failed event payload
- **WHEN** enrollment fails at step 2 with FULL error
- **THEN** `ENROLLMENT_FAILED` event includes step=2 and error_code=FULL

### Requirement: LED feedback for retry
The executor SHALL call LED feedback functions when retry occurs.

#### Scenario: LED retry feedback
- **WHEN** RETRY_STEP action is returned
- **THEN** `led_enrollment_step_retry()` is called before re-executing step

### Requirement: LED feedback for completion
The executor SHALL provide appropriate LED feedback for success and failure.

#### Scenario: LED success feedback
- **WHEN** COMPLETE action is returned
- **THEN** `led_enrollment_success_green()` is called

#### Scenario: LED failure feedback
- **WHEN** FAIL action is returned
- **THEN** `led_enrollment_failed()` is called