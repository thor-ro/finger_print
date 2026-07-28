## Design: Consolidate Enrollment State Machines (Complete Integration)

## Context

The `sdf_state_machines` component has been enhanced with `sdf_enrollment_sm_apply_step_result_ex()` returning action-based decisions. The `sdf_enroll_task` in `sdf_services_enroll.c` currently uses the legacy `sdf_enrollment_sm_apply_step_result()` and needs to be refactored to use the enhanced API with proper action routing.

## Goals / Non-Goals

**Goals:**
- Refactor enrollment task to use enhanced SM API
- Implement executor loop handling all action types
- Ensure proper event emission with correct payloads

**Non-Goals:**
- Changing the state machine logic (already done)
- Adding new retry configurations (already done)

## Decisions

### Executor Loop Pattern
Use a stateless executor pattern where the SM returns the next action to take. The task maintains current state and calls `apply_step_result_ex()` on each driver result.

### Event Payload Structure
Use existing `sdf_event_router_enrollment_complete_payload_t` (user_id, permission) and `sdf_event_router_enrollment_failed_payload_t` (step, error_code) defined in `sdf_event_router.h`.

## Risks / Trade-offs

- **State reset timing**: Need to ensure SM is reset after COMPLETE/FAIL before next enrollment start
- **Event ordering**: Events must be emitted before SM reset to capture correct state