# Tasks: Consolidate Enrollment State Machines (Complete Integration)

## Phase 1: Refactor sdf_services_enroll to use enhanced API

- [x] 1.1 Update sdf_enroll_task to use sdf_enrollment_sm_apply_step_result_ex
- [x] 1.2 Replace legacy sdf_enrollment_sm_apply_step_result calls
- [x] 1.3 Implement executor loop handling EXECUTE_STEP action
- [x] 1.4 Implement RETRY_STEP action with LED feedback
- [x] 1.5 Implement COMPLETE action with event emission
- [x] 1.6 Implement FAIL action with event emission

## Phase 2: Add LED feedback functions

- [x] 2.1 Add led_enrollment_step_retry() to led.h/led.c (orange blink)
- [x] 2.2 Add led_enrollment_failed() to led.h/led.c (red flash)

## Phase 3: Verify event emission

- [x] 3.1 Ensure ENROLLMENT_COMPLETE emits with correct payload (user_id, permission)
- [x] 3.2 Ensure ENROLLMENT_FAILED emits with correct payload (step, error_code)
- [x] 3.3 Wire events to sdf_app handler (already partially done)

## Phase 4: Cleanup and testing

- [x] 4.1 Remove unused sdf_services_enrollment_state_name functions if present
- [x] 4.2 Add integration tests for full enrollment flow
- [x] 4.3 Add integration tests for retry on ACK_FAIL step 1
- [x] 4.4 Add integration tests for failure on step 3
- [ ] 4.5 Build and verify for ESP32-C6 target (requires ESP-IDF environment)

## Phase 5: Documentation

- [x] 5.1 Update sdf_sas.md §5 (component responsibilities)
- [x] 5.2 Update sdf_sas.md §10 (enrollment flow diagram)

## Phase 6: Cleanup legacy code

- [x] 6.1 Remove dead legacy sdf_services_task function (boot init code moved to sdf_match_task)
- [x] 6.2 Remove unused task defines (SDF_SERVICES_TASK_NAME, etc.)