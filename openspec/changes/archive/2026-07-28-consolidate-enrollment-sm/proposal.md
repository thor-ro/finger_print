# Proposal: Consolidate Enrollment State Machines

## Summary

Merge the two enrollment state machine implementations into one authoritative implementation in `sdf_state_machines`, with `sdf_services` acting purely as executor (driver calls, LED feedback, event emission).

## Problem

Two enrollment state machines exist:

### 1. `sdf_state_machines` (`sdf_enrollment_sm_*`)
- Pure logic, no hardware dependencies
- 5 states: `IDLE` → `STEP_1` → `STEP_2` → `STEP_3` → `SUCCESS/ERROR`
- Functions: `init`, `start`, `apply_step_result`, `is_active`, `current_step`, `current_command`
- **Well-tested** (3 unit tests)

### 2. `sdf_services_enrollment` (`sdf_services_enrollment.c`)
- Executes steps via `fp_enroll_step()`
- Handles retries (ACK_FAIL on step 1-2)
- Manages `sdf_enrollment_sm_t` instance internally
- LED feedback per step
- Calls `sdf_enrollment_sm_apply_step_result()`
- **Duplicates state logic**: retry counting, step advancement, completion detection

**Issues:**
- Logic split across boundaries
- `sdf_services` knows enrollment SM internals (checks `state == STEP_1` etc.)
- Retry logic duplicated (SM handles some, services handles some)
- Hard to test enrollment execution without full services stack
- `sdf_enrollment_sm` not reusable without `sdf_services`

## Solution

### Single Source of Truth: `sdf_state_machines`

Enhance `sdf_enrollment_sm` to be complete:
- Internal retry/self-contained
- Exposes `next_action` (what to execute) instead of `current_step`
- Handles all retry logic internally
- Returns deterministic `next_command` for driver

### `sdf_services` Becomes Pure Executor

- Holds `sdf_enrollment_sm_t` instance
- On `ENROLLMENT_START` event: calls `sm_start()`
- On driver result: calls `sm_apply_step_result()` → gets `next_action`
- Executes `next_action` via `fp_enroll_step()` / `led_*()`
- Emits events: `ENROLLMENT_STEP_COMPLETE`, `ENROLLMENT_COMPLETE`, `ENROLLMENT_FAILED`

### Enhanced SM API

```c
// sdf_state_machines.h
typedef enum {
  SDF_ENROLL_ACT_NONE,
  SDF_ENROLL_ACT_EXECUTE_STEP,   // Run fp_enroll_step(cmd, user_id, perm)
  SDF_ENROLL_ACT_RETRY_STEP,      // Re-run same step (ACK_FAIL)
  SDF_ENROLL_ACT_COMPLETE,        // Success - emit COMPLETE
  SDF_ENROLL_ACT_FAIL,            // Failure - emit FAILED
} sdf_enroll_action_t;

typedef struct {
  sdf_enroll_action_t action;
  sdf_fingerprint_enroll_step_t cmd;  // For EXECUTE_STEP
  uint16_t user_id;
  uint8_t permission;
  uint8_t retry_count;                // For RETRY_STEP
} sdf_enroll_next_t;

// Core API
void sdf_enrollment_sm_init(sdf_enrollment_sm_t *sm);
void sdf_enrollment_sm_start(sdf_enrollment_sm_t *sm, uint16_t user_id, uint8_t permission);
sdf_enroll_next_t sdf_enrollment_sm_apply_step_result(sdf_enrollment_sm_t *sm, 
                                                       sdf_fingerprint_op_result_t result);
bool sdf_enrollment_sm_is_active(const sdf_enrollment_sm_t *sm);
sdf_enrollment_state_t sdf_enrollment_sm_get_state(const sdf_enrollment_sm_t *sm);
uint8_t sdf_enrollment_sm_get_completed_steps(const sdf_enrollment_sm_t *sm);

// Retry policy (configurable via struct init)
typedef struct {
  uint8_t max_retries_step1;  // default: 3
  uint8_t max_retries_step2;  // default: 3
  uint8_t max_retries_step3;  // default: 0 (no retry on step 3)
} sdf_enrollment_retry_policy_t;
```

### State Machine Logic (Internal)

```
IDLE
  └── start(user_id, perm) → STEP_1, action=EXECUTE_STEP(cmd=STEP_1)

STEP_1
  ├── apply_step_result(OK) → STEP_2, action=EXECUTE_STEP(cmd=STEP_2)
  ├── apply_step_result(ACK_FAIL) → retry<max? RETRY_STEP : FAIL
  └── apply_step_result(OTHER) → FAIL

STEP_2
  ├── apply_step_result(OK) → STEP_3, action=EXECUTE_STEP(cmd=STEP_3)
  ├── apply_step_result(ACK_FAIL) → retry<max? RETRY_STEP : FAIL
  └── apply_step_result(OTHER) → FAIL

STEP_3
  ├── apply_step_result(OK) → SUCCESS, action=COMPLETE
  └── apply_step_result(ANY_FAIL) → FAIL, action=FAIL

SUCCESS/ERROR → IDLE (on next start)
```

### sdf_services Execution Loop

```c
// In sdf_services_task or dedicated enroll_task
void sdf_enrollment_executor(void) {
  sdf_enroll_next_t next = sdf_enrollment_sm_apply_step_result(&sm, driver_result);
  
  switch (next.action) {
    case SDF_ENROLL_ACT_EXECUTE_STEP:
      // Power on sensor if needed
      fp_set_power(true);
      fp_wait_for_power_ready();
      driver_result = fp_enroll_step(next.cmd, next.user_id, next.permission);
      // Loop continues with driver_result
      break;
      
    case SDF_ENROLL_ACT_RETRY_STEP:
      ESP_LOGW(TAG, "Enrollment step %d retry %d/%d", 
               sm.current_step, next.retry_count, max_retries);
      led_enrollment_step_retry();
      driver_result = fp_enroll_step(next.cmd, next.user_id, next.permission);
      break;
      
    case SDF_ENROLL_ACT_COMPLETE:
      ESP_LOGI(TAG, "Enrollment complete for user %d", next.user_id);
      led_enrollment_success();
      sdf_event_emit(SDF_EVT_ENROLLMENT_COMPLETE, &(user_id){next.user_id});
      sdf_enrollment_sm_init(&sm);  // Reset
      break;
      
    case SDF_ENROLL_ACT_FAIL:
      ESP_LOGE(TAG, "Enrollment failed at step %d", sm.current_step);
      led_enrollment_failed();
      sdf_event_emit(SDF_EVT_ENROLLMENT_FAILED, &(user_id){next.user_id});
      sdf_enrollment_sm_init(&sm);  // Reset
      break;
      
    case SDF_ENROLL_ACT_NONE:
      // Wait for next driver result or start event
      break;
  }
}
```

## Migration Plan

### Phase 1: Enhance `sdf_state_machines`
1. Add `sdf_enroll_next_t` and `sdf_enroll_action_t` types
2. Add retry policy to `sdf_enrollment_sm_t` struct
3. Implement `sdf_enrollment_sm_apply_step_result()` with full retry logic
4. Add unit tests for all retry scenarios
5. Keep existing API for backward compatibility

### Phase 2: Simplify `sdf_services_enrollment`
1. Remove internal retry logic
2. Remove direct SM state checks (`sm.state == STEP_1`)
3. Replace with executor loop using `sdf_enrollment_sm_apply_step_result()`
4. Emit events instead of calling callbacks directly

### Phase 3: Update Consumers
1. `sdf_app` subscribes to `ENROLLMENT_COMPLETE/FAILED` events
2. Remove `enrollment_cb` from `sdf_services_config_t`
3. Update admin action flow to use events

### Phase 4: Cleanup
1. Remove `sdf_services_enrollment.c/.h` (fold into `sdf_services.c`)
2. Remove duplicate `sdf_services_enrollment_state_name` etc.
3. Update documentation

## Benefits

1. **Single source of truth**: Enrollment logic in one place
2. **Reusable SM**: `sdf_state_machines` usable without `sdf_services`
3. **Testable**: Pure logic unit tested; executor integration tested separately
4. **Configurable retries**: Policy in SM, not hardcoded
5. **Clear separation**: Logic vs. execution
6. **Event-driven**: Fits event router architecture

## Acceptance Criteria

- [ ] `sdf_enrollment_sm_apply_step_result()` handles all retry logic
- [ ] `sdf_services` has no enrollment state logic (only executor)
- [ ] Unit tests: 100% coverage of SM retry transitions
- [ ] Integration test: full 3-step enrollment via events
- [ ] Integration test: retry on ACK_FAIL step 1, then success
- [ ] Integration test: failure on step 3 → FAILED event
- [ ] `sdf_services_enrollment.c` removed or < 100 lines
- [ ] Documentation updated (sdf_sas.md §5, §10)
- [ ] No regression in existing enrollment flow