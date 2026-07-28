# Design: Consolidate Enrollment State Machines

## Overview

This design implements the proposal to merge the two enrollment state machine implementations into a single authoritative implementation in `sdf_state_machines`, with `sdf_services` acting purely as an executor.

## Architecture Changes

### Before (Current State)

```
┌─────────────────────────────────────────────────────────────┐
│ sdf_services_enrollment.c                                    │
│ - sdf_services_run_enrollment_step()                         │
│   - Contains retry logic (inline)                            │
│   - Checks sm.state == STEP_1 etc.                           │
│   - Calls fp_enroll_step() directly                          │
│   - Calls sdf_enrollment_sm_apply_step_result()              │
│ - sdf_services_start_pending_enrollment_if_any()             │
│ - sdf_services_request_enrollment()                          │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│ sdf_state_machines.c                                         │
│ - sdf_enrollment_sm_t (5 states)                             │
│ - sdf_enrollment_sm_apply_step_result()                      │
│   - Advances state on OK                                     │
│   - Silently retries on ACK_FAIL (steps 1-2)                 │
│   - Fails on ACK_FAIL (step 3) or other errors               │
│ - No retry counting, no configurability                      │
└─────────────────────────────────────────────────────────────┘
```

### After (Target State)

```
┌─────────────────────────────────────────────────────────────┐
│ sdf_services.c (enrollment executor)                         │
│ - sdf_services_run_enrollment_step()                         │
│   - NO retry logic                                           │
│   - NO direct state checks                                   │
│   - Calls sm_apply_step_result() → gets next_action          │
│   - Switches on next_action:                                 │
│     * EXECUTE_STEP → fp_enroll_step()                        │
│     * RETRY_STEP → led_retry() + fp_enroll_step()            │
│     * COMPLETE → emit ENROLLMENT_COMPLETE                    │
│     * FAIL → emit ENROLLMENT_FAILED                          │
│ - sdf_services_start_pending_enrollment_if_any()             │
│ - sdf_services_request_enrollment()                          │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│ sdf_state_machines.c                                         │
│ - sdf_enrollment_sm_t (extended with retry counters)         │
│ - sdf_enrollment_retry_policy_t (configurable)               │
│ - sdf_enrollment_sm_apply_step_result_ex()                   │
│   - Returns sdf_enroll_next_t {action, cmd, user_id, perm}   │
│   - Handles ALL retry logic internally                       │
│   - Returns retry_count for logging                          │
│ - sdf_enrollment_sm_init_with_policy()                       │
└─────────────────────────────────────────────────────────────┘
```

## API Design

### New Types (sdf_state_machines.h)

```c
// Action returned by apply_step_result
typedef enum {
  SDF_ENROLL_ACT_NONE = 0,
  SDF_ENROLL_ACT_EXECUTE_STEP = 1,
  SDF_ENROLL_ACT_RETRY_STEP = 2,
  SDF_ENROLL_ACT_COMPLETE = 3,
  SDF_ENROLL_ACT_FAIL = 4,
} sdf_enroll_action_t;

// Next action to execute
typedef struct {
  sdf_enroll_action_t action;
  sdf_fingerprint_enroll_step_t cmd;
  uint16_t user_id;
  uint8_t permission;
  uint8_t retry_count;
} sdf_enroll_next_t;

// Configurable retry policy
typedef struct {
  uint8_t max_retries_step1;  // default: 3
  uint8_t max_retries_step2;  // default: 3
  uint8_t max_retries_step3;  // default: 0
} sdf_enrollment_retry_policy_t;

// Extended state machine struct
typedef struct {
  sdf_enrollment_state_t state;
  sdf_enrollment_result_t result;
  uint16_t user_id;
  uint8_t permission;
  uint8_t completed_steps;
  uint8_t retry_count_step1;
  uint8_t retry_count_step2;
  uint8_t retry_count_step3;
  sdf_enrollment_retry_policy_t retry_policy;
} sdf_enrollment_sm_t;

// Default policy macro
#define SDF_ENROLLMENT_DEFAULT_RETRY_POLICY \
  { .max_retries_step1 = 3, .max_retries_step2 = 3, .max_retries_step3 = 0 }
```

### New API Functions

```c
// Initialize with custom retry policy (NULL for defaults)
void sdf_enrollment_sm_init_with_policy(sdf_enrollment_sm_t *sm,
                                        const sdf_enrollment_retry_policy_t *policy);

// Enhanced apply_step_result - returns next action
sdf_enroll_next_t sdf_enrollment_sm_apply_step_result_ex(
    sdf_enrollment_sm_t *sm, sdf_fingerprint_op_result_t step_result);

// New getters
sdf_enrollment_state_t sdf_enrollment_sm_get_state(const sdf_enrollment_sm_t *sm);
uint8_t sdf_enrollment_sm_get_completed_steps(const sdf_enrollment_sm_t *sm);
```

### Backward Compatibility

- Existing `sdf_enrollment_sm_apply_step_result()` retained as wrapper
- All existing tests pass without modification
- Default retry policy matches old behavior (3 retries on steps 1-2, 0 on step 3)

## State Machine Logic (Internal)

```
IDLE
  └── start(user_id, perm) → STEP_1, action=EXECUTE_STEP(cmd=STEP_1)

STEP_1
  ├── apply_step_result(OK) → STEP_2, action=EXECUTE_STEP(cmd=STEP_2)
  ├── apply_step_result(ACK_FAIL) → retry_count < max? RETRY_STEP : FAIL
  └── apply_step_result(OTHER) → FAIL

STEP_2
  ├── apply_step_result(OK) → STEP_3, action=EXECUTE_STEP(cmd=STEP_3)
  ├── apply_step_result(ACK_FAIL) → retry_count < max? RETRY_STEP : FAIL
  └── apply_step_result(OTHER) → FAIL

STEP_3
  ├── apply_step_result(OK) → SUCCESS, action=COMPLETE
  └── apply_step_result(ANY_FAIL) → FAIL, action=FAIL

SUCCESS/ERROR → IDLE (on next start)
```

## sdf_services Executor Loop

```c
void sdf_services_run_enrollment_step(void) {
  sdf_services_state_t *state = sdf_services_state();
  sdf_enroll_next_t next = {0};

  while (true) {
    // Acquire lock, check active, copy state, release
    // ...
    
    sdf_fingerprint_op_result_t step_result = SDF_FINGERPRINT_OP_FAILED;
    
    switch (next.action) {
    case SDF_ENROLL_ACT_EXECUTE_STEP:
      fp_set_power(true);
      step_result = fp_enroll_step(next.cmd, next.user_id, next.permission);
      break;
      
    case SDF_ENROLL_ACT_RETRY_STEP:
      ESP_LOGW(TAG, "Enrollment step %u retry %u/%u", ...);
      led_enrollment_step_retry();
      step_result = fp_enroll_step(next.cmd, next.user_id, next.permission);
      break;
      
    case SDF_ENROLL_ACT_COMPLETE:
      ESP_LOGI(TAG, "Enrollment complete for user %u", next.user_id);
      led_enrollment_success_green();
      sdf_services_emit_enrollment_event(SDF_EVENT_ROUTER_ENROLLMENT_COMPLETE, ...);
      // Reset SM
      return;
      
    case SDF_ENROLL_ACT_FAIL:
      ESP_LOGE(TAG, "Enrollment failed at step %u", ...);
      led_enrollment_failed();
      sdf_services_emit_enrollment_event(SDF_EVENT_ROUTER_ENROLLMENT_FAILED, ...);
      // Reset SM
      return;
      
    case SDF_ENROLL_ACT_NONE:
    default:
      // First iteration: get initial EXECUTE_STEP for step 1
      next = sdf_enrollment_sm_apply_step_result_ex(&sm, SDF_FINGERPRINT_OP_OK);
      continue;
    }
    
    // Apply driver result to SM
    next = sdf_enrollment_sm_apply_step_result_ex(&sm, step_result);
    
    // Handle LED feedback
    // ...
    
    if (step_result != SDF_FINGERPRINT_OP_OK) {
      return; // Non-retryable error
    }
  }
}
```

## Event Definitions

### New Events (sdf_event_router.h)

```c
typedef enum {
  // ... existing ...
  SDF_EVENT_ROUTER_ENROLLMENT_START,
  SDF_EVENT_ROUTER_ENROLLMENT_STEP_RESULT,
  SDF_EVENT_ROUTER_ENROLLMENT_COMPLETE,   // NEW
  SDF_EVENT_ROUTER_ENROLLMENT_FAILED,     // NEW
} sdf_event_router_type_t;

// Payload for COMPLETE
typedef struct {
  uint16_t user_id;
  uint8_t permission;
} sdf_event_router_enrollment_complete_payload_t;

// Payload for FAILED
typedef struct {
  uint8_t step;
  int8_t error_code;
} sdf_event_router_enrollment_failed_payload_t;
```

## sdf_app Consumer Updates

- Remove `enrollment_cb` from `sdf_services_config_t`
- Subscribe to `ENROLLMENT_COMPLETE` and `ENROLLMENT_FAILED` events
- Update Zigbee user list on COMPLETE event
- Handle FAILED event for alarm/alerting

## LED Feedback Additions

```c
// led.h
void led_enrollment_step_retry(void);  // Orange blink
void led_enrollment_failed(void);      // Red flash
```

## Configuration

Retry policy configurable via `sdf_enrollment_sm_init_with_policy()`. Defaults match current behavior:
- Step 1: 3 retries (ACK_FAIL)
- Step 2: 3 retries (ACK_FAIL)
- Step 3: 0 retries (immediate fail on any error)

## Testing Strategy

### Unit Tests (sdf_state_machines/test/)
- All existing tests pass (backward compatibility)
- New tests for enhanced API:
  - `test_enrollment_sm_retry_on_ack_fail_step1()`
  - `test_enrollment_sm_retry_on_ack_fail_step2()`
  - `test_enrollment_sm_max_retries_exceeded_step1()`
  - `test_enrollment_sm_ack_fail_step3_fails_immediately()`
  - `test_enrollment_sm_enhanced_api_success_sequence()`
  - `test_enrollment_sm_enhanced_api_failure_at_step2()`
  - `test_enrollment_sm_custom_retry_policy()`
  - `test_enrollment_sm_default_retry_policy()`
  - `test_enrollment_sm_get_state()`
  - `test_enrollment_sm_get_completed_steps()`
  - `test_enrollment_sm_legacy_api_still_works()`
  - `test_enrollment_sm_legacy_api_retry_behavior()`

### Integration Tests (test_runner)
- Full 3-step enrollment via events
- Retry on ACK_FAIL step 1 then success
- Failure on step 3 → FAILED event
- Admin enrollment flow with permission=3

## Migration Checklist

- [x] Phase 1: Enhance sdf_state_machines
- [x] Phase 2: Simplify sdf_services_enrollment
- [x] Phase 3: Update sdf_app consumer
- [x] Phase 4: Cleanup (remove sdf_services_enrollment.c/.h)
- [x] Documentation: Update sdf_sas.md §5, §10
- [x] All tests pass
- [x] Firmware builds for ESP32-C6 target