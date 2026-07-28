# Spec: Power Policy API

## Component: sdf_power_policy

### Public Header: `include/sdf_power_policy.h`

```c
#ifndef SDF_POWER_POLICY_H
#define SDF_POWER_POLICY_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    SDF_POWER_POLICY_WAKE_REASON_TIMER = 0,
    SDF_POWER_POLICY_WAKE_REASON_FINGERPRINT = 1,
    SDF_POWER_POLICY_WAKE_REASON_OTHER = 2,
} sdf_power_policy_wake_reason_t;

typedef enum {
    SDF_POWER_POLICY_DECISION_SLEEP_LIGHT = 0,
    SDF_POWER_POLICY_DECISION_SLEEP_DEEP = 1,
    SDF_POWER_POLICY_DECISION_STAY_AWAKE = 2,
} sdf_power_policy_decision_t;

typedef struct sdf_power_policy_config_t {
    uint32_t checkin_interval_ms;
    uint32_t idle_before_sleep_ms;
    uint32_t post_wake_guard_ms;
    uint32_t loop_interval_ms;
    uint32_t battery_report_interval_ms;
    bool enable_light_sleep;
    bool enable_ble_radio_gating;
    bool enable_deep_sleep_fallback;
    int fp_wake_gpio;
    
    bool (*busy_cb)(void *ctx);
    void (*wake_cb)(void *ctx, sdf_power_policy_wake_reason_t);
    int (*battery_cb)(void *ctx);
    bool (*zigbee_ready_cb)(void *ctx);
    
    void *ctx;
} sdf_power_policy_config_t;

void sdf_power_policy_init(const sdf_power_policy_config_t *config);
sdf_power_policy_decision_t sdf_power_policy_evaluate(int64_t now_us, int64_t last_activity_us,
                                             int64_t wake_guard_until_us, int64_t next_battery_report_us);
void sdf_power_policy_mark_activity(void);
uint8_t sdf_power_policy_get_battery_percent(void);
void sdf_power_policy_handle_wake(sdf_power_policy_wake_reason_t reason);
bool sdf_power_policy_is_ready(void);
int64_t sdf_power_policy_get_last_activity_us(void);
int64_t sdf_power_policy_get_wake_guard_until_us(void);
int64_t sdf_power_policy_get_next_battery_report_us(void);

#endif /* SDF_POWER_POLICY_H */
```

### Function Contracts

#### `sdf_power_policy_init`
- **Precondition:** config is not NULL, config values are within valid ranges
- **Postcondition:** Internal state initialized, callbacks registered

#### `sdf_power_policy_evaluate`
- **Returns:** `SDF_POWER_DECISION_SLEEP_LIGHT` if all conditions met for light sleep
- **Returns:** `SDF_POWER_DECISION_SLEEP_DEEP` if light sleep allowed but Zigbee not ready
- **Returns:** `SDF_POWER_DECISION_STAY_AWAKE` otherwise

**Evaluation conditions for light sleep:**
1. `enable_light_sleep` is true
2. `now_us >= wake_guard_until_us` (wake guard expired)
3. `now_us - last_activity_us >= idle_before_sleep_ms * 1000` (idle timeout)
4. `busy_cb` returns false (system not busy)
5. Not in Linux simulation (no USB-Serial-JTAG check in policy)

**Evaluation conditions for deep sleep fallback:**
1. All light sleep conditions met
2. `enable_deep_sleep_fallback` is true
3. `zigbee_ready_cb` returns false (Zigbee not joined)

#### `sdf_power_policy_mark_activity`
- Updates `last_activity_us` to current time
- No return value

#### `sdf_power_policy_handle_wake`
- Invokes `wake_cb` if registered
- Updates `wake_guard_until_us` based on `post_wake_guard_ms`
- Handles battery report triggering on timer wake

### Kconfig Options (referenced from sdf_config)
All values come from `sdf_config.h`:
- `CONFIG_SDF_CHECKIN_INTERVAL_MS`
- `CONFIG_SDF_IDLE_BEFORE_SLEEP_MS`
- `CONFIG_SDF_POST_WAKE_GUARD_MS`
- `CONFIG_SDF_POWER_LOOP_INTERVAL_MS`
- `CONFIG_SDF_BATTERY_REPORT_INTERVAL_MS`
- `CONFIG_SDF_ENABLE_LIGHT_SLEEP`
- `CONFIG_SDF_ENABLE_BLE_RADIO_GATING`
- `CONFIG_SDF_ENABLE_DEEP_SLEEP_FALLBACK`