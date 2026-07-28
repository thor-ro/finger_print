# Tasks: Split Power Policy from Platform

## Task List

- [x] Create `sdf_platform_power` component directory structure and CMakeLists.txt
- [x] Create `sdf_platform_power.h` header with platform API
- [x] Implement `sdf_platform_power.c` with ESP-IDF sleep wrappers
- [x] Implement `sdf_platform_power_linux.c` mock for host testing
- [x] Create `sdf_power_policy` component directory structure and CMakeLists.txt
- [x] Create `sdf_power_policy.h` header with portable policy API
- [x] Implement `sdf_power_policy.c` with extracted decision logic
- [x] Create `test_sdf_power_policy.c` with Unity unit tests
- [x] Refactor `sdf_power_task` to use `sdf_power_policy_evaluate()`
- [x] Update `sdf_app` to initialize both components
- [x] Move `sdf_power_management_task` to `sdf_app` or dedicated location
- [x] Update `sdf_power` to be thin adapter (deprecated)
- [x] Add `zigbee_ready_cb` callback to policy config
- [x] Wire battery callback for battery percentage reading
- [x] Update documentation (sdf_sas.md §5, §8, §11)

## Implementation Notes

### Task Loop Logic (from proposal)
```c
void sdf_power_management_task(void *arg) {
    while (true) {
        int64_t now = esp_timer_get_time();
        
        sdf_power_decision_t decision = sdf_power_policy_evaluate(
            now, 
            sdf_power_policy_get_last_activity_us(),
            sdf_power_policy_get_wake_guard_until_us(),
            sdf_power_policy_get_next_battery_report_us());
        
        switch (decision) {
            case SDF_POWER_DECISION_SLEEP_LIGHT:
                sdf_platform_power_enable_timer_wake(checkin_interval_ms);
                if (fp_wake_gpio >= 0) {
                    sdf_platform_power_enable_gpio_wake(fp_wake_gpio, 1);
                }
                if (ble_radio_gating) sdf_platform_power_gate_ble_radio(false);
                sdf_platform_power_enter_light();
                if (ble_radio_gating) sdf_platform_power_gate_ble_radio(true);
                sdf_power_policy_handle_wake(SDF_POWER_WAKE_REASON_TIMER);
                break;
                
            case SDF_POWER_DECISION_SLEEP_DEEP:
                sdf_platform_power_disable_all_wake();
                if (fp_wake_gpio >= 0) {
                    sdf_platform_power_enable_gpio_wake_deep(fp_wake_gpio, 1);
                }
                sdf_platform_power_enter_deep();
                break;
                
            case SDF_POWER_DECISION_STAY_AWAKE:
                // Policy handles battery reporting internally
                break;
        }
        
        vTaskDelay(loop_interval_ms);
    }
}
```

### Acceptance Criteria Mapping
- Task 1-3 → `sdf_power_policy` compiles and tests on Linux host
- Task 4-6 → `sdf_platform_power` has ESP and Linux implementations  
- Task 7-8 → Policy evaluation covers all current sleep conditions
- Task 9 → Deep sleep fallback logic preserved
- Task 10 → BLE radio gating works via platform API
- Task 11-12 → Battery reporting triggers Zigbee attribute update