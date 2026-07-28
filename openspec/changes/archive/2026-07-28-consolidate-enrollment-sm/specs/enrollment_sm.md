# Spec: Consolidate Enrollment State Machines

## Scope
Merge the two enrollment state machine implementations (`sdf_state_machines` and `sdf_services_enrollment`) into a single authoritative implementation in `sdf_state_machines`, with `sdf_services` becoming a pure executor.

## Acceptance Criteria

### Functional
- [ ] `sdf_enrollment_sm_apply_step_result_ex()` handles all retry logic internally
- [ ] `sdf_services` has NO enrollment state logic (only executor)
- [ ] Retry policy configurable via `sdf_enrollment_retry_policy_t`
- [ ] Default retry: 3 on step 1, 3 on step 2, 0 on step 3
- [ ] Step 3 ACK_FAIL fails immediately (no retry)
- [ ] Events emitted: `ENROLLMENT_COMPLETE`, `ENROLLMENT_FAILED`
- [ ] Backward compatibility: legacy API works unchanged

### Quality
- [ ] Unit tests: 100% coverage of SM retry transitions
- [ ] Integration test: full 3-step enrollment via events
- [ ] Integration test: retry on ACK_FAIL step 1, then success
- [ ] Integration test: failure on step 3 → FAILED event
- [ ] `sdf_services_enrollment.c` removed or < 100 lines
- [ ] Documentation updated (sdf_sas.md §5, §10)

### Non-Functional
- [ ] Firmware builds for ESP32-C6 target
- [ ] No binary size regression
- [ ] No runtime performance regression

## Test Scenarios

| Scenario | Expected Behavior |
|----------|-------------------|
| Start enrollment → 3 OK results | COMPLETE event, user enrolled |
| Start → OK → ACK_FAIL (x3) → OK → OK | RETRY_STEP 3x, then COMPLETE |
| Start → OK → OK → ACK_FAIL (step 3) | FAIL immediately, FAILED event |
| Start → Timeout (step 1) | FAIL immediately, FAILED event |
| Start → OK → Full (step 2) | FAIL immediately, FAILED event |
| Custom policy: step1=1, step2=2 | Only 1 retry on step 1, 2 on step 2 |
| Legacy API: ACK_FAIL on step 1 | Silent retry (backward compat) |
| Legacy API: ACK_FAIL on step 3 | Fail (backward compat) |