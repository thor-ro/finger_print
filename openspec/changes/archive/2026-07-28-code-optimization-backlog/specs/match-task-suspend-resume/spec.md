## ADDED Requirements

### Requirement: Match task uses suspend flag + extended poll interval
The match task SHALL replace the WDT delete/recreate + `portMAX_DELAY` semaphore-block with a suspend flag and an extended poll interval when idle.

#### Scenario: Match task suspends when no activity is pending
- **WHEN** the system determines no match activity is needed (no pending request, not in cooldown, not in lockout, no enrollment active)
- **THEN** it sets `s_match_state.suspended = true` and continues the main loop with an extended poll interval (10 seconds or power manager check-in interval)
- **AND** the WDT remains running and the task is not blocked on a semaphore

#### Scenario: Match task resumes on power wake event
- **WHEN** a `SDF_EVENT_ROUTER_POWER_WAKE` event is received while suspended
- **THEN** `s_match_state.suspended` is set to `false`
- **AND** the poll interval reverts to the normal `SDF_MATCH_POLL_MS` (400ms)

#### Scenario: Match task resumes on match request event
- **WHEN** a `SDF_EVENT_ROUTER_BIOMETRIC_MATCH_REQUEST` event is received while suspended
- **THEN** `s_match_state.suspended` is set to `false`
- **AND** `s_match_state.pending_match_request` is set to `true`
- **AND** the next cycle runs a match immediately

#### Scenario: WDT is never deleted or recreated during sleep transitions
- **WHEN** the match task enters the idle/suspended state
- **THEN** the WDT remains active and reset normally each loop iteration
- **AND** no semaphore is blocked with `portMAX_DELAY`