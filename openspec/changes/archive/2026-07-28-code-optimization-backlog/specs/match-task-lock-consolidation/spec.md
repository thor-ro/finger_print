## ADDED Requirements

### Requirement: Match cycle consolidates lock acquisitions to 2 per cycle
`sdf_match_task_run_match_cycle()` SHALL reduce lock acquisitions from 6+ to 2 per invocation: one at the start for config+state read, one at the end for state write.

#### Scenario: Config read uses single lock acquisition
- **WHEN** the match cycle begins
- **THEN** it acquires the lock once, reads all config values (cooldown_ms, failed_attempt_threshold, failed_attempt_window_ms, lockout_duration_ms) and state values (enrolled_user_count, lockout_until_us, match_cooldown_until_us, failed_attempt_count, failed_attempt_window_start_us), then releases the lock before any I/O

#### Scenario: State writes use single lock acquisition
- **WHEN** the match cycle completes (success, no-match, or error)
- **THEN** it acquires the lock once, writes all state updates (cooldown_until_us, failed_attempt_count, failed_attempt_window_start_us, lockout_until_us), then releases the lock

### Requirement: Lockout-cleared event emission is deduplicated
The lockout-cleared event SHALL be emitted from a single code path after config reads and I/O complete, not duplicated across early-return and post-I/O branches.

#### Scenario: Lockout cleared after config read
- **WHEN** the lockout timer expires and the cycle proceeds past the early-return check
- **THEN** the lockout-cleared event is emitted exactly once
- **AND** it is not re-emitted after the fingerprint match result returns

#### Scenario: Lockout cleared but fingerprint sensor is busy
- **WHEN** the lockout timer expires and the cycle early-returns due to cooldown or enrollment
- **THEN** the lockout-cleared event is emitted in the early-return path
- **AND** it is NOT emitted again after the sensor returns a result