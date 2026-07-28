## Power State Consistency Tasks

### 1. Fix sdf_power_policy_init()
- [ ] Replace mock `1000000000` initialization with `0` for all timestamps
- [ ] Initialize `last_activity_us = 0`, `wake_guard_until_us = 0`, `next_battery_report_us = 0`

### 2. Fix sdf_power_policy_mark_activity()
- [ ] Replace `s_state.last_activity_us = 1000000000` with `s_state.last_activity_us = esp_timer_get_time()`
- [ ] Include `esp_timer.h` header if not already present

### 3. Fix sdf_power_policy_handle_wake()
- [ ] Replace `s_state.wake_guard_until_us = 1000000000 + ...` with `s_state.wake_guard_until_us = esp_timer_get_time() + ...`
- [ ] Replace `s_state.last_activity_us = 1000000000` with `s_state.last_activity_us = esp_timer_get_time()`
- [ ] Replace `s_state.next_battery_report_us = 1000000000 + ...` with `s_state.next_battery_report_us = esp_timer_get_time() + ...`

### 4. Fix sdf_power_policy_init() wake guard calculation
- [ ] Replace `s_state.wake_guard_until_us = s_state.last_activity_us + ...` (which uses mock 1e9) with a proper deferred initialization or real-time value

### 5. Update sdf_power_policy_get_last_activity_us()
- [ ] Verify it returns real time (already does, just a getter)

### 6. Build and Test
- [ ] Build with debug config
- [ ] Flash to hardware
- [ ] Verify deep sleep enters after idle timeout (5s default)
- [ ] Verify wake guard prevents immediate re-sleep after wake
- [ ] Verify battery report fires at 60s intervals
- [ ] Verify deep sleep fallback when Zigbee not joined

### 7. Documentation Sync
- [ ] Update `doc/rtos_tasks.md` if power policy timing details changed
- [ ] Update `doc/sdf_sas.md` §8.2 if power management behavior changed