# Design: Fix Power Policy Mock Time Inconsistency

## Context

The ESP32-C6 firmware uses a dual-component power management architecture:
- `sdf_power.c` — runtime task using real `esp_timer_get_time()` 
- `sdf_power_policy.c` — policy decision logic using a mock timestamp `1000000000`

All time-based decisions in the policy component (idle detection, wake guard, battery report scheduling) compare real timestamps from `sdf_power_task` against mock timestamps from `sdf_power_policy`, producing incorrect results.

## Decision

Replace all mock timestamps in `sdf_power_policy.c` with real `esp_timer_get_time()` values. Make the policy component a pure function of real time deltas.

## Rationale

1. **Idle detection**: Currently `now_us - last_activity_us` evaluates to ~1e9 seconds (mock minus real), which is always "not idle"
2. **Wake guard**: Currently compares real `now_us` against mock `wake_guard_until_us`, always passes
3. **Battery report**: Currently schedules based on mock time, never fires at correct interval
4. **Deep sleep fallback**: Policy decision is based on nonsensical timestamps

## Implementation

### sdf_power_policy_init()
Replace mock initialization:
```c
// Before
s_state.last_activity_us = 1000000000;
s_state.wake_guard_until_us = s_state.last_activity_us + ...;

// After  
s_state.last_activity_us = 0;  // Unset; first mark_activity() sets it
s_state.wake_guard_until_us = 0;
```

### sdf_power_policy_mark_activity()
Replace mock time with real time:
```c
// Before
s_state.last_activity_us = 1000000000;

// After
s_state.last_activity_us = esp_timer_get_time();
```

### sdf_power_policy_handle_wake()
Replace mock time with real time:
```c
// Before
s_state.wake_guard_until_us = 1000000000 + ...;
s_state.last_activity_us = 1000000000;

// After
s_state.wake_guard_until_us = esp_timer_get_time() + ...;
s_state.last_activity_us = esp_timer_get_time();
```

### sdf_power_policy_evaluate()
The function body stays the same — it already uses real `now_us` from its callers. The only change is that `last_activity_us`, `wake_guard_until_us`, and `next_battery_report_us` now come from real time instead of mock time.

## Risk Mitigation

- **Low risk**: The policy component is stateless except for its internal timestamps
- **Test coverage**: The change is internal to `sdf_power_policy.c` — all existing tests should pass
- **Backward compatible**: No API changes; the policy function receives the same timestamp arguments