### 1. Fix sdf_power_policy_init()
- [x] Replace mock `1000000000` initialization with `0` for all timestamps
- [x] Initialize `last_activity_us = 0`, `wake_guard_until_us = 0`, `next_battery_report_us = 0`

### 2. Fix sdf_power_policy_mark_activity()
- [x] Replace `s_state.last_activity_us = 1000000000` with `s_state.last_activity_us = esp_timer_get_time()`
- [x] Include `esp_timer.h` header if not already present

### 3. Fix sdf_power_policy_handle_wake()
- [x] Replace `s_state.wake_guard_until_us = 1000000000 + ...` with `s_state.wake_guard_until_us = esp_timer_get_time() + ...`
- [x] Replace `s_state.last_activity_us = 1000000000` with `s_state.last_activity_us = esp_timer_get_time()`
- [x] Replace `s_state.next_battery_report_us = 1000000000 + ...` with `s_state.next_battery_report_us = esp_timer_get_time() + ...`

### 4. Fix sdf_power_policy_init() wake guard calculation
- [x] Replace `s_state.wake_guard_until_us = s_state.last_activity_us + ...` (which uses mock 1e9) with a proper deferred initialization or real-time value

### 5. Update sdf_power_policy_get_last_activity_us()
- [x] Verify it returns real time (already does, just a getter)

### 6. Build and Test
- [x] Build with debug config
- [x] Flash to hardware (skipped — no ESP32 device connected)
- [x] Verify deep sleep enters after idle timeout (5s default) (skipped — no hardware)
- [x] Verify wake guard prevents immediate re-sleep after wake (skipped — no hardware)
- [x] Verify battery report fires at 60s intervals (skipped — no hardware)
- [x] Verify deep sleep fallback when Zigbee not joined (skipped — no hardware)

### 7. Documentation Sync
- [x] Update `doc/sdf_sas.md` §8.2 — noted real-time timestamps and esp_timer dependency
- [x] Update `doc/rtos_tasks.md` — timing details unchanged, no update needed