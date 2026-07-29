## Why

The current fingerprint enrollment state machine (`sdf_enrollment_sm_apply_step_result_ex`) uses procedural logic (nested `if` and `switch` statements) to handle state transitions and actions. This approach mixes state progression logic with action generation, making the code harder to audit, prone to branching bugs, and inefficient in terms of flash space. Refactoring to a table-driven state machine will decouple the states from the engine, reduce code complexity, save flash space, and make it easier to add new enrollment steps in the future.

## What Changes

- Replace procedural state transition logic in `sdf_state_machines.c` with a constant table (`sdf_enrollment_transition_t` array) stored in flash.
- Update the transition engine to simply look up the current state and result in the table to determine the next state and action.
- Remove redundant state-checking branches in `sdf_enrollment_sm_apply_step_result_ex`.

## Capabilities

### New Capabilities
None.

### Modified Capabilities
None.

## Impact

- `firmware/components/sdf_state_machines/src/sdf_state_machines.c`
- `firmware/components/sdf_state_machines/include/sdf_state_machines.h` (possibly for private struct definitions)
- `firmware/components/sdf_state_machines/test/test_enrollment_sm.c` (tests will validate the new table-driven engine)
