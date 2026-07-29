## Power State Consistency Specification

### Current State (Mock Time)

`sdf_power_policy.c` uses a fixed mock timestamp for all time calculations:
```c
s_state.last_activity_us = 1000000000;  // Never changes after init
s_state.wake_guard_until_us = 1000000000 + post_wake_guard_ms * 1000LL;
s_state.next_battery_report_us = 1000000000;
```

`sdf_power.c` uses real time:
```c
int64_t now_us = esp_timer_get_time();
```

### Target State (Real Time)

All timestamps in `sdf_power_policy` use `esp_timer_get_time()`:
```c
s_state.last_activity_us = esp_timer_get_time();  // Updated on mark_activity()
s_state.wake_guard_until_us = esp_timer_get_time() + post_wake_guard_ms * 1000LL;
s_state.next_battery_report_us = esp_timer_get_time() + battery_report_interval_ms * 1000LL;
```

### Impact on sdf_power_policy_evaluate()

The evaluate function receives real timestamps from its callers:
```c
sdf_power_policy_decision_t sdf_power_policy_evaluate(
    int64_t now_us, int64_t last_activity_us,
    int64_t wake_guard_until_us, int64_t next_battery_report_us);
```

With mock time, `last_activity_us` was always ~1e9, so `now_us - last_activity_us` was always negative or near zero — never triggering idle sleep. With real time, the difference correctly reflects actual idle duration.

### Key Functions Affected

| Function | Change Required |
|---------|----------------|
| `sdf_power_policy_init()` | Initialize timestamps to 0 (unset) |
| `sdf_power_policy_mark_activity()` | Use `esp_timer_get_time()` |
| `sdf_power_policy_handle_wake()` | Use `esp_timer_get_time()` for wake guard and last activity |
| `sdf_power_policy_evaluate()` | No change needed (already uses real timestamps from callers) |

### Verification Points

1. Idle sleep triggers when device is actually idle for > idle_before_sleep_ms
2. Wake guard expires after actual post_wake_guard_ms
3. Battery report fires at correct intervals (60s default)
4. Deep sleep fallback triggers correctly when Zigbee not ready
5. Power policy decisions align with sdf_power_task's real time calculations