# Proposal: Consolidate Enrollment State Machines

## Summary

Merge the two enrollment state machine implementations into one authoritative implementation in `sdf_state_machines`, with `sdf_services` acting purely as executor (driver calls, LED feedback, event emission).

## Problem

Two enrollment state machines exist:

### 1. `sdf_state_machines` (`sdf_enrollment_sm_*`)
- Pure logic, no hardware dependencies
- 5 states: `IDLE` → `STEP_1` → `STEP_2` → `STEP_3` → `SUCCESS/ERROR`
- Functions: `init`, `start`, `apply_step_result`, `is_active`, `current_step`, `current_command`
- **Partially enhanced** with `sdf_enrollment_sm_apply_step_result_ex()` and retry policy

### 2. `sdf_services_enrollment` (`sdf_services_enroll.c`)
- Executes steps via `fp_enroll_step()`
- Still subscribes to `ENROLLMENT_START` and `ENROLLMENT_STEP_RESULT` events
- **Not yet refactored** to use enhanced SM API (`sdf_enrollment_sm_apply_step_result_ex`)
- Missing: `sdf_services_run_enrollment_step()` implementation using new API

**Issues:**
- `sdf_services_enroll.c` still uses legacy `sdf_enrollment_sm_apply_step_result()` 
- Executor logic not updated to use `sdf_enroll_next_t` return values
- Event emission for COMPLETE/FAILED exists but not wired to enhanced API
- Tasks 2-4 and documentation updates still pending

## Solution

### Complete the Enhanced SM Integration

The `sdf_state_machines` component has been enhanced with:
- `sdf_enroll_action_t` and `sdf_enroll_next_t` types
- Retry counters and policy in `sdf_enrollment_sm_t`
- `sdf_enrollment_sm_apply_step_result_ex()` returning next action
- `sdf_enrollment_sm_init_with_policy()` for custom retry config
- Getters: `sdf_enrollment_sm_get_state()`, `sdf_enrollment_sm_get_completed_steps()`

What remains to complete Phase 2-4:

### Phase 2: Simplify `sdf_services_enroll` (incomplete)

Refactor `sdf_services_run_enrollment_step()` to use the enhanced API:
- Use `sdf_enrollment_sm_apply_step_result_ex()` instead of legacy
- Remove internal retry logic (now in SM)
- Remove direct SM state checks (`sm.state == STEP_1`)
- Implement executor loop: EXECUTE_STEP → RETRY_STEP → COMPLETE/FAIL actions
- Properly emit `ENROLLMENT_COMPLETE`/`ENROLLMENT_FAILED` events

### Phase 3: Update Consumers (partial)

- `sdf_app` already subscribes to `ENROLLMENT_COMPLETE`/`ENROLLMENT_FAILED` events
- Update Zigbee user sync on COMPLETE event (verify correct user_id/permission)
- Remove any remaining legacy callback references

### Phase 4: Cleanup

- Simplify or remove `sdf_services_enrollment.c` if fully merged
- Update documentation (sdf_sas.md §5, §10)

## Benefits

1. **Single source of truth**: All retry logic in `sdf_state_machines`
2. **Reusable SM**: State machine usable independently
3. **Testable**: Enhanced SM already unit tested; executor needs integration tests
4. **Configurable retries**: Policy already in SM struct
5. **Event-driven**: Already integrated with event router

## Acceptance Criteria

- [ ] `sdf_services_run_enrollment_step()` uses enhanced API
- [ ] Executor loop properly handles EXECUTE_STEP, RETRY_STEP, COMPLETE, FAIL actions
- [ ] LED feedback functions work: `led_enrollment_step_retry()`, `led_enrollment_failed()`
- [ ] Events emitted correctly with proper payloads
- [ ] All existing unit tests pass
- [ ] Integration tests for enhanced enrollment flow
- [ ] Documentation updated