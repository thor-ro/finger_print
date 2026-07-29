## 1. Table definitions

- [x] 1.1 Add `sdf_enrollment_transition_t` struct and `transitions` array to `sdf_state_machines.c`

## 2. Refactoring Engine

- [x] 2.1 Refactor `sdf_enrollment_sm_apply_step_result_ex` to iterate over the `transitions` table
- [x] 2.2 Add explicit retry counter logic in the failure path of the engine
- [x] 2.3 Remove old procedural `if/switch` branching for state transitions

## 3. Verification

- [x] 3.1 Run `test_enrollment_sm` to ensure all state machine tests pass
