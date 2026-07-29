#include "sdf_power_policy.h"

#include <string.h>
#include <stdint.h>
#include <esp_timer.h>

#define SDF_POWER_POLICY_LOCK_WAIT_MS 250u

typedef struct {
    bool initialized;
    sdf_power_policy_config_t config;
    int64_t last_activity_us;
    int64_t wake_guard_until_us;
    int64_t next_battery_report_us;
    uint8_t battery_percent;
} sdf_power_policy_state_t;

static sdf_power_policy_state_t s_state = {0};

void sdf_power_policy_init(const sdf_power_policy_config_t *config) {
    if (config == NULL) {
        return;
    }
    s_state.config = *config;
    s_state.initialized = true;
    s_state.last_activity_us = 0;
    s_state.wake_guard_until_us = 0;
    s_state.next_battery_report_us = 0;
}

sdf_power_policy_decision_t sdf_power_policy_evaluate(int64_t now_us, int64_t last_activity_us,
                                             int64_t wake_guard_until_us, int64_t next_battery_report_us) {
    if (!s_state.initialized || !s_state.config.enable_light_sleep) {
        return SDF_POWER_POLICY_DECISION_STAY_AWAKE;
    }

    bool allow_sleep = true;

    /* Wake guard check */
    if (now_us < wake_guard_until_us) {
        allow_sleep = false;
    }

    /* Idle timeout check */
    if (allow_sleep &&
        (now_us - last_activity_us) <
            ((int64_t)s_state.config.idle_before_sleep_ms * 1000LL)) {
        allow_sleep = false;
    }

    /* Busy callback check */
    if (allow_sleep && s_state.config.busy_cb != NULL && 
        s_state.config.busy_cb(s_state.config.ctx)) {
        allow_sleep = false;
    }

    /* Check for battery report */
    if (now_us >= next_battery_report_us && s_state.config.battery_cb != NULL) {
        int battery_cb_result = s_state.config.battery_cb(s_state.config.ctx);
        if (battery_cb_result >= 0 && battery_cb_result <= 100) {
            s_state.battery_percent = (uint8_t)battery_cb_result;
        }
    }

    if (!allow_sleep) {
        return SDF_POWER_POLICY_DECISION_STAY_AWAKE;
    }

    /* Deep sleep fallback check */
    if (s_state.config.enable_deep_sleep_fallback &&
        s_state.config.zigbee_ready_cb != NULL &&
        !s_state.config.zigbee_ready_cb(s_state.config.ctx)) {
        return SDF_POWER_POLICY_DECISION_SLEEP_DEEP;
    }

    return SDF_POWER_POLICY_DECISION_SLEEP_LIGHT;
}

void sdf_power_policy_mark_activity(void) {
    s_state.last_activity_us = esp_timer_get_time();
}

uint8_t sdf_power_policy_get_battery_percent(void) {
    return s_state.battery_percent;
}

void sdf_power_policy_handle_wake(sdf_power_policy_wake_reason_t reason) {
    (void)reason;
    /* Update wake guard */
    s_state.wake_guard_until_us = esp_timer_get_time() + 
        ((int64_t)s_state.config.post_wake_guard_ms * 1000LL);
    
    /* Update last activity */
    s_state.last_activity_us = esp_timer_get_time();
    
    /* Invoke callback */
    if (s_state.config.wake_cb != NULL) {
        s_state.config.wake_cb(s_state.config.ctx, reason);
    }
    
    /* Schedule next battery report */
    s_state.next_battery_report_us = esp_timer_get_time() + 
        ((int64_t)s_state.config.battery_report_interval_ms * 1000LL);
}

bool sdf_power_policy_is_ready(void) {
    return s_state.initialized;
}

int64_t sdf_power_policy_get_last_activity_us(void) {
    return s_state.last_activity_us;
}

int64_t sdf_power_policy_get_wake_guard_until_us(void) {
    return s_state.wake_guard_until_us;
}

int64_t sdf_power_policy_get_next_battery_report_us(void) {
    return s_state.next_battery_report_us;
}