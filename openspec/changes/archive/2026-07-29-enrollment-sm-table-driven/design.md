## Context

The `sdf_enrollment_sm_apply_step_result_ex` function currently uses procedural logic to transition between states in the enrollment state machine. The state machine is isolated enough that transitioning it to a table-driven approach will simplify the code, reduce the bug surface area for state transitions, and save flash space by defining the state transitions as a constant array rather than compiled branches.

## Goals / Non-Goals

**Goals:**
- Replace the procedural transition logic in `sdf_enrollment_sm_apply_step_result_ex` with a table-driven approach.
- Maintain identical state machine behavior, including step retries and failure states.
- Save flash space and simplify future additions to the state machine.

**Non-Goals:**
- Do not change how the state machine integrates with `sdf_services_enroll.c` or locks.
- Do not modify the underlying fingerprint sensor commands or protocol.

## Decisions

- **Table Structure**: We will introduce a `sdf_enrollment_transition_t` struct containing `current_state`, `result`, `next_state`, and `action`.
- **Engine Logic**: `sdf_enrollment_sm_apply_step_result_ex` will iterate through a constant array of transitions, matching the current state and result. If a match is found, it updates the state and returns the specified action.
- **Retry Logic**: Handling step retries (e.g., repeating a scan) involves checking the retry counter in the state machine struct. The engine will handle this logic explicitly when a failure occurs to avoid exploding the state table with counter-specific states, keeping the table clean and focused on high-level transitions.

## Risks / Trade-offs

- **Risk**: Test suite breakages.
  - **Mitigation**: We will ensure `test_enrollment_sm.c` passes without modification to the test logic itself.
- **Trade-off**: The table lookup adds a small loop overhead compared to direct branches.
  - **Mitigation**: The table is extremely small (around 4-6 entries), so the iteration overhead is negligible.
