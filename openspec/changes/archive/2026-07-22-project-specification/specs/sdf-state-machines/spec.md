## ADDED Requirements

### Requirement: Enrollment State Machine
The `sdf_state_machines` component SHALL implement a pure-logic enrollment state machine with states: IDLE, STEP_1, STEP_2, STEP_3, SUCCESS, ERROR.

#### Scenario: Enrollment state transitions
- **WHEN** `sdf_enrollment_sm_start(user_id, permission)` called
- **THEN** State: IDLE → STEP_1
- **WHEN** `sdf_enrollment_sm_apply_step_result(FP_ACK_OK)` in STEP_1
- **THEN** State: STEP_1 → STEP_2
- **WHEN** `sdf_enrollment_sm_apply_step_result(FP_ACK_OK)` in STEP_2
- **THEN** State: STEP_2 → STEP_3
- **WHEN** `sdf_enrollment_sm_apply_step_result(FP_ACK_OK)` in STEP_3
- **THEN** State: STEP_3 → SUCCESS

#### Scenario: Enrollment retry on fail
- **WHEN** `sdf_enrollment_sm_apply_step_result(FP_ACK_FAIL)` in STEP_1 or STEP_2
- **THEN** State: unchanged (retry same step)
- **THEN** Caller should re-invoke `fp_enroll_step()` for same step

#### Scenario: Enrollment fail on final step
- **WHEN** `sdf_enrollment_sm_apply_step_result(FP_ACK_FAIL)` in STEP_3
- **THEN** State: STEP_3 → ERROR
- **THEN** Enrollment failed (templates incompatible)

#### Scenario: Enrollment abort
- **WHEN** `sdf_enrollment_sm_abort()` called in any state
- **THEN** State: → ERROR
- **THEN** Cleanup any partial enrollment

### Requirement: Device State Machine
The `sdf_state_machines` component SHALL implement a device state machine tracking: UNCLAIMED, CLAIMED, ENROLLING, PAIRING, JOINING, LOCKOUT.

#### Scenario: Device state transitions
- **WHEN** Boot with 0 enrolled users: state = UNCLAIMED
- **WHEN** Boot with >0 enrolled users: state = CLAIMED
- **WHEN** Admin enrollment started: state = ENROLLING
- **WHEN** Enrollment success: state = CLAIMED
- **WHEN** Pairing started: state = PAIRING
- **WHEN** Pairing success: state = CLAIMED
- **WHEN** Zigbee join started: state = JOINING
- **WHEN** Join success: state = CLAIMED
- **WHEN** Biometric lockout triggered: state = LOCKOUT
- **WHEN** Lockout expires: state = CLAIMED (or UNCLAIMED if 0 users)

### Requirement: State Machine API
The `sdf_state_machines` component SHALL provide pure functions with no hardware dependencies for testability.

#### Scenario: State machine API
- **WHEN** `sdf_enrollment_sm_init()` called
- **THEN** Returns initialized state machine context
- **WHEN** `sdf_enrollment_sm_get_state(ctx)` called
- **THEN** Returns current state enum
- **WHEN** `sdf_enrollment_sm_reset(ctx)` called
- **THEN** State machine reset to IDLE

### Requirement: Admin Auth State Machine
The `sdf_state_machines` component SHALL track admin authorization state: IDLE, PENDING, AUTHORIZED, EXPIRED, DENIED.

#### Scenario: Admin auth states
- **WHEN** Button press sets pending action: state = PENDING
- **WHEN** Admin fingerprint matched: state = AUTHORIZED
- **WHEN** 10s timeout: state = EXPIRED
- **WHEN** Non-admin fingerprint: state = DENIED (briefly), then back to PENDING