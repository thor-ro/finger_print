# Proposal: Fix Power Policy Mock Time Inconsistency

## Summary

The `sdf_power_policy` component uses a fixed mock timestamp (`1000000000`) for all time calculations, while `sdf_power_task` uses real `esp_timer_get_time()`. This inconsistency means power policy decisions are never based on actual elapsed time, causing incorrect sleep/wake behavior.

## Problem

In `sdf_power_policy.c`, all timestamps are initialized to a mock value:
```c
s_state.last_activity_us = 1000000000;  // Mock time
s_state.wake_guard_until_us = 1000000000 + post_wake_guard_ms * 1000LL;
s_state.next_battery_report_us = 1000000000;
```

While `sdf_power_task` uses real time:
```c
int64_t now_us = esp_timer_get_time();
```

This causes:
1. **Idle detection never triggers**: `now_us - last_activity_us` is always ~-1e9 (negative), so device never sleeps from idle
2. **Wake guard never expires**: Comparison `now_us >= wake_guard_until_us` uses real time vs mock, always passes immediately
3. **Battery report scheduling broken**: `next_battery_report_us` never reaches current real time
4. **Deep sleep fallback unreliable**: Policy decisions use nonsensical relative timestamps

## Solution

Replace mock time values with real `esp_timer_get_time()` in `sdf_power_policy.c`. Initialize timestamps to 0 (unset) and update them with real time on each activity/wake event.

## Architecture Impact

### Modify
- `sdf_power_policy.c`: Replace all `1000000000` mock timestamps with `esp_timer_get_time()`
- `sdf_power_policy_mark_activity()`: Use real time instead of mock time
- `sdf_power_policy_handle_wake()`: Use real time for wake guard scheduling
- `sdf_power_policy_init()`: Initialize timestamps to 0 (unset) instead of mock value

### Simplify
- `sdf_power_policy_evaluate()` becomes a true function of real time deltas
- No more "mock time" constant needed in code

## API Design

No API changes. Function signatures remain the same; only internal time source changes.

## Benefits

1. **Correct sleep decisions**: Power policy evaluates actual idle time
2. **Reliable wake guards**: Wake guard timeout actually works
3. **Accurate battery reporting**: Battery report interval based on real time
4. **Simpler code**: No mock time constant needed; policy is a pure function of elapsed time

## Acceptance Criteria

- [ ] `sdf_power_policy_init()` no longer sets mock time
- [ ] `sdf_power_policy_mark_activity()` records real time
- [ ] `sdf_power_policy_handle_wake()` schedules using real time
- [ ] `sdf_power_policy_evaluate()` makes correct decisions with real timestamps
- [ ] Idle sleep triggers when device is actually idle for > idle_before_sleep_ms
- [ ] Wake guard expires after actual post_wake_guard_ms
- [ ] Battery report fires at correct 60s intervals
- [ ] Deep sleep fallback triggers correctly when Zigbee not joined