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