# Tasks: Consolidate Enrollment State Machines

## Phase 1: Enhance sdf_state_machines
- [x] Add `sdf_enroll_action_t`, `sdf_enroll_next_t`, `sdf_enrollment_retry_policy_t` types
- [x] Extend `sdf_enrollment_sm_t` with retry counters and policy
- [x] Implement `sdf_enrollment_sm_apply_step_result_ex()` with full retry logic
- [x] Add `sdf_enrollment_sm_init_with_policy()` for custom retry config
- [x] Add getters: `sdf_enrollment_sm_get_state()`, `sdf_enrollment_sm_get_completed_steps()`
- [x] Preserve legacy `sdf_enrollment_sm_apply_step_result()` for backward compat
- [x] Add comprehensive unit tests for all retry scenarios

## Phase 2: Simplify sdf_services_enrollment
- [x] Remove internal retry logic from `sdf_services_run_enrollment_step()`
- [x] Refactor to executor loop using `apply_step_result_ex()`
- [x] Add LED feedback functions: `led_enrollment_step_retry()`, `led_enrollment_failed()`
- [x] Emit new events: `ENROLLMENT_COMPLETE`, `ENROLLMENT_FAILED`
- [x] Remove direct SM state checks (`sm.state == STEP_1`)

## Phase 3: Update Consumers
- [x] Remove `enrollment_cb` from `sdf_services_config_t`
- [x] Update `sdf_app` to subscribe to `ENROLLMENT_COMPLETE`/`ENROLLMENT_FAILED`
- [x] Update Zigbee user sync on COMPLETE event
- [x] Remove `sdf_app_on_enrollment_state()` callback

## Phase 4: Cleanup
- [x] Delete `sdf_services_enrollment.c` and `sdf_services_enrollment.h`
- [x] Move public enrollment API to `sdf_services.h`
- [x] Remove unused functions: `sdf_services_enrollment_state_name()`, etc.

## Documentation
- [x] Update sdf_sas.md §5 (component responsibilities)
- [x] Update sdf_sas.md §5.2 (sdf_services whitebox)
- [x] Update sdf_sas.md §6.3 (enrollment flow diagram)

## Verification
- [x] Firmware builds for ESP32-C6
- [x] All existing unit tests pass
- [x] New unit tests pass