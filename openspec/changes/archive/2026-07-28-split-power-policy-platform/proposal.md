# Proposal: Split Power Policy from Platform

## Summary

Refactor `sdf_power` into two components:
- `sdf_power_policy` - Sleep/wake decisions, battery reporting, Zigbee coordination (portable, testable)
- `sdf_platform_power` - ESP-IDF sleep API wrappers, GPIO wake config, timer wake (platform-specific)

## Problem

Current `sdf_power.c` (380 lines) mixes:
- **Policy**: When to sleep, wake guard timing, battery report intervals, busy callbacks
- **Mechanism**: `esp_sleep_enable_timer_wakeup()`, `esp_sleep_enable_gpio_wakeup()`, `esp_light_sleep_start()`

**Issues:**
- Cannot unit test sleep logic without ESP-IDF mocks
- Linux simulation (`CONFIG_IDF_TARGET_LINUX`) requires `#ifdef` sprinkled throughout
- Zigbee/BLE radio gating logic intertwined with platform sleep calls
- Hard to port to other MCUs or RTOS

## Solution

### Component Split

```
firmware/components/
├── sdf_power_policy/
│   ├── include/sdf_power_policy.h
│   ├── src/sdf_power_policy.c
│   └── test/test_sdf_power_policy.c
├── sdf_platform_power/
│   ├── include/sdf_platform_power.h
│   ├── src/sdf_platform_power.c
│   └── src/sdf_platform_power_linux.c  (mock)
```

### API Boundary

```c
// sdf_power_policy.h - Pure policy, no ESP-IDF types
typedef struct {
    uint32_t checkin_interval_ms;
    uint32_t idle_before_sleep_ms;
    uint32_t post_wake_guard_ms;
    uint32_t loop_interval_ms;
    uint32_t battery_report_interval_ms;
    bool enable_light_sleep;
    bool enable_ble_radio_gating;
    bool enable_deep_sleep_fallback;
    int fp_wake_gpio;           // GPIO number only
    
    // Callbacks (policy-driven)
    bool (*busy_cb)(void *ctx);           // Is system busy?
    void (*wake_cb)(void *ctx, wake_reason_t);  // Wake notification
    int (*battery_cb)(void *ctx);         // Read battery %
    void *ctx;
} sdf_power_policy_config_t;

// Policy decisions
typedef enum {
    SDF_POWER_DECISION_SLEEP_LIGHT,
    SDF_POWER_DECISION_SLEEP_DEEP,
    SDF_POWER_DECISION_STAY_AWAKE,
} sdf_power_decision_t;

// Core API
void sdf_power_policy_init(const sdf_power_policy_config_t *config);
sdf_power_decision_t sdf_power_policy_evaluate(int64_t now_us, int64_t last_activity_us, 
                                               int64_t wake_guard_until_us, int64_t next_battery_report_us);
void sdf_power_policy_mark_activity(void);
uint8_t sdf_power_policy_get_battery_percent(void);
void sdf_power_policy_handle_wake(wake_reason_t reason);
bool sdf_power_policy_is_ready(void);
```

```c
// sdf_platform_power.h - Platform mechanism
typedef enum {
    SDF_PLATFORM_WAKE_TIMER,
    SDF_PLATFORM_WAKE_GPIO,
    SDF_PLATFORM_WAKE_OTHER,
} sdf_platform_wake_reason_t;

// Platform API
esp_err_t sdf_platform_power_init(void);
esp_err_t sdf_platform_power_enable_timer_wake(uint32_t interval_ms);
esp_err_t sdf_platform_power_enable_gpio_wake(int gpio_num, int level);
esp_err_t sdf_platform_power_disable_all_wake(void);
esp_err_t sdf_platform_power_enter_light(void);
esp_err_t sdf_platform_power_enter_deep(void);
sdf_platform_wake_reason_t sdf_platform_power_get_wake_reason(void);
esp_err_t sdf_platform_power_gate_ble_radio(bool enable);  // Platform-specific BLE gate
```

### Integration in `sdf_app`

```c
// sdf_app_init() - wire policy + platform
sdf_power_policy_config_t policy_cfg = {
    .checkin_interval_ms = sdf_config_get()->checkin_interval_ms,
    .idle_before_sleep_ms = sdf_config_get()->idle_before_sleep_ms,
    .post_wake_guard_ms = sdf_config_get()->post_wake_guard_ms,
    .loop_interval_ms = sdf_config_get()->loop_interval_ms,
    .battery_report_interval_ms = sdf_config_get()->battery_report_interval_ms,
    .enable_light_sleep = sdf_config_get()->enable_light_sleep,
    .enable_ble_radio_gating = sdf_config_get()->enable_ble_radio_gating,
    .enable_deep_sleep_fallback = sdf_config_get()->enable_deep_sleep_fallback,
    .fp_wake_gpio = sdf_config_get()->fp_wake_gpio,
    .busy_cb = sdf_app_power_busy,
    .wake_cb = sdf_app_power_wakeup,
    .battery_cb = sdf_app_power_battery_percent,
    .ctx = NULL,
};

sdf_power_policy_init(&policy_cfg);
sdf_platform_power_init();  // Platform init
```

### Task Loop (in sdf_app or dedicated task)

```c
void sdf_power_management_task(void *arg) {
    while (true) {
        int64_t now = esp_timer_get_time();
        
        // Policy evaluates - NO platform calls
        sdf_power_decision_t decision = sdf_power_policy_evaluate(
            now, last_activity, wake_guard_until, next_battery_report);
        
        // Platform executes decision
        switch (decision) {
            case SDF_POWER_DECISION_SLEEP_LIGHT:
                sdf_platform_power_enable_timer_wake(checkin_interval_ms);
                if (fp_wake_gpio >= 0) {
                    sdf_platform_power_enable_gpio_wake(fp_wake_gpio, 1);
                }
                if (ble_radio_gating) sdf_platform_power_gate_ble_radio(false);
                sdf_platform_power_enter_light();
                if (ble_radio_gating) sdf_platform_power_gate_ble_radio(true);
                break;
                
            case SDF_POWER_DECISION_SLEEP_DEEP:
                sdf_platform_power_disable_all_wake();
                if (fp_wake_gpio >= 0) {
                    sdf_platform_power_enable_gpio_wake_deep(fp_wake_gpio, 1);
                }
                sdf_platform_power_enter_deep();
                break;
                
            case SDF_POWER_DECISION_STAY_AWAKE:
            default:
                // Check battery report
                if (now >= next_battery_report) {
                    uint8_t bat = sdf_power_policy_get_battery_percent();
                    sdf_power_policy_handle_wake(SDF_POWER_WAKE_TIMER);  // Triggers Zigbee report
                }
                break;
        }
        
        vTaskDelay(loop_interval_ms);
    }
}
```

### Linux Mock (`sdf_platform_power_linux.c`)

```c
// Full mock for host testing
esp_err_t sdf_platform_power_init(void) { return ESP_OK; }
esp_err_t sdf_platform_power_enable_timer_wake(uint32_t ms) { 
    mock_next_wake_ms = ms; 
    return ESP_OK; 
}
esp_err_t sdf_platform_power_enter_light(void) { 
    mock_sleep_count++; 
    usleep(1000);  // Simulate quick wake
    return ESP_OK; 
}
sdf_platform_wake_reason_t sdf_platform_power_get_wake_reason(void) {
    return mock_wake_reason;
}
```

## Benefits

| Aspect | Before | After |
|--------|--------|-------|
| **Testability** | Requires ESP-IDF + hardware | Pure C unit tests on host |
| **Portability** | ESP32-C6 only | Policy reusable, platform swappable |
| **Linux CI** | Complex `#ifdef` | Clean mock implementation |
| **Separation** | Mixed policy/mechanism | Clean architecture boundary |
| **Debugging** | Hard to trace decisions | `sdf_power_decision_t` logged |

## Migration Steps

1. Create `sdf_platform_power` component with ESP and Linux implementations
2. Create `sdf_power_policy` component with decision logic
3. Extract decision logic from `sdf_power_task` → `sdf_power_policy_evaluate()`
4. Extract platform calls → `sdf_platform_power_*()`
5. Update `sdf_app` to initialize both
6. Update `sdf_power` → deprecate (keep as thin adapter for transition)
7. Move tests: policy tests in `sdf_power_policy/test/`, platform tests in `sdf_platform_power/test/`
8. Update documentation (sdf_sas.md §5, §8, §11)

## Acceptance Criteria

- [ ] `sdf_power_policy` compiles and tests on Linux host (no ESP-IDF)
- [ ] `sdf_platform_power` has ESP and Linux implementations
- [ ] Policy evaluation covers all current sleep conditions
- [ ] Deep sleep fallback logic preserved
- [ ] BLE radio gating works via platform API
- [ ] Battery reporting triggers Zigbee attribute update
- [ ] Wake reason mapping preserved
- [ ] Unit tests: >90% coverage on policy logic
- [ ] Integration test: sleep → wake → report cycle

## Risks

- **Task location**: Where does power management task live? 
  - *Decision*: Keep in `sdf_app` as `sdf_power_management_task` (orchestrates policy+platform)
- **Zigbee check-in**: Policy needs Zigbee readiness for deep sleep fallback
  - *Solution*: Add `bool (*zigbee_ready_cb)(void *ctx)` to policy config
- **Mutex sharing**: State shared between policy evaluation and platform
  - *Solution*: Policy owns state, platform is stateless