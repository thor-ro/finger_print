#include "unity.h"
#include <string.h>

#include "sdf_power_policy.h"

static sdf_power_policy_config_t s_test_config;
static bool s_mock_busy = false;
static bool s_mock_zigbee_ready = true;
static int s_mock_battery = 85;

static bool mock_busy_cb(void *ctx) {
    (void)ctx;
    return s_mock_busy;
}

static bool mock_zigbee_ready_cb(void *ctx) {
    (void)ctx;
    return s_mock_zigbee_ready;
}

static int mock_battery_cb(void *ctx) {
    (void)ctx;
    return s_mock_battery;
}

void setUp(void) {
    memset(&s_test_config, 0, sizeof(s_test_config));
    s_test_config.checkin_interval_ms = 15000;
    s_test_config.idle_before_sleep_ms = 5000;
    s_test_config.post_wake_guard_ms = 1500;
    s_test_config.loop_interval_ms = 100;
    s_test_config.battery_report_interval_ms = 60000;
    s_test_config.enable_light_sleep = true;
    s_test_config.enable_ble_radio_gating = false;
    s_test_config.enable_deep_sleep_fallback = true;
    s_test_config.fp_wake_gpio = -1;
    s_test_config.busy_cb = mock_busy_cb;
    s_test_config.zigbee_ready_cb = mock_zigbee_ready_cb;
    s_test_config.battery_cb = mock_battery_cb;
    s_test_config.ctx = NULL;

    s_mock_busy = false;
    s_mock_zigbee_ready = true;
    s_mock_battery = 85;
}

void tearDown(void) {
    /* Reset state */
}

void test_power_policy_decision_sleep_light_when_all_conditions_met(void) {
    sdf_power_policy_init(&s_test_config);
    
    int64_t now_us = 1000000000;
    int64_t last_activity_us = now_us - 10000000;  /* 10s ago (beyond idle_before_sleep) */
    int64_t wake_guard_until_us = now_us - 1000;     /* Expired */
    int64_t next_battery_report_us = now_us + 1000000; /* Not due yet */
    
    sdf_power_policy_decision_t decision = sdf_power_policy_evaluate(
        now_us, last_activity_us, wake_guard_until_us, next_battery_report_us);
    
    TEST_ASSERT_EQUAL(SDF_POWER_POLICY_DECISION_SLEEP_LIGHT, decision);
}

void test_power_policy_decision_stay_awake_when_wake_guard_active(void) {
    sdf_power_policy_init(&s_test_config);
    
    int64_t now_us = 1000000000;
    int64_t last_activity_us = now_us - 10000000;
    int64_t wake_guard_until_us = now_us + 2000000;  /* Still active */
    int64_t next_battery_report_us = now_us + 1000000;
    
    sdf_power_policy_decision_t decision = sdf_power_policy_evaluate(
        now_us, last_activity_us, wake_guard_until_us, next_battery_report_us);
    
    TEST_ASSERT_EQUAL(SDF_POWER_POLICY_DECISION_STAY_AWAKE, decision);
}

void test_power_policy_decision_stay_awake_when_busy(void) {
    sdf_power_policy_init(&s_test_config);
    
    int64_t now_us = 1000000000;
    int64_t last_activity_us = now_us - 10000000;
    int64_t wake_guard_until_us = now_us - 1000;
    int64_t next_battery_report_us = now_us + 1000000;
    
    s_mock_busy = true;
    sdf_power_policy_decision_t decision = sdf_power_policy_evaluate(
        now_us, last_activity_us, wake_guard_until_us, next_battery_report_us);
    
    TEST_ASSERT_EQUAL(SDF_POWER_POLICY_DECISION_STAY_AWAKE, decision);
}

void test_power_policy_decision_deep_sleep_when_zigbee_not_ready(void) {
    sdf_power_policy_init(&s_test_config);
    
    int64_t now_us = 1000000000;
    int64_t last_activity_us = now_us - 10000000;
    int64_t wake_guard_until_us = now_us - 1000;
    int64_t next_battery_report_us = now_us + 1000000;
    
    s_mock_zigbee_ready = false;
    sdf_power_policy_decision_t decision = sdf_power_policy_evaluate(
        now_us, last_activity_us, wake_guard_until_us, next_battery_report_us);
    
    TEST_ASSERT_EQUAL(SDF_POWER_POLICY_DECISION_SLEEP_DEEP, decision);
}

void test_power_policy_decision_stay_awake_when_light_sleep_disabled(void) {
    s_test_config.enable_light_sleep = false;
    sdf_power_policy_init(&s_test_config);
    
    int64_t now_us = 1000000000;
    int64_t last_activity_us = now_us - 10000000;
    int64_t wake_guard_until_us = now_us - 1000;
    int64_t next_battery_report_us = now_us + 1000000;
    
    sdf_power_policy_decision_t decision = sdf_power_policy_evaluate(
        now_us, last_activity_us, wake_guard_until_us, next_battery_report_us);
    
    TEST_ASSERT_EQUAL(SDF_POWER_POLICY_DECISION_STAY_AWAKE, decision);
}

void test_power_policy_mark_activity_updates_timestamp(void) {
    sdf_power_policy_init(&s_test_config);
    
    int64_t before = sdf_power_policy_get_last_activity_us();
    sdf_power_policy_mark_activity();
    int64_t after = sdf_power_policy_get_last_activity_us();
    
    TEST_ASSERT_GREATER_THAN_INT64(before, 0);
    TEST_ASSERT_GREATER_THAN_INT64(after, before);
}

void test_power_policy_handle_wake_sets_guard(void) {
    sdf_power_policy_init(&s_test_config);
    
    int64_t before_guard = sdf_power_policy_get_wake_guard_until_us();
    sdf_power_policy_handle_wake(SDF_POWER_POLICY_WAKE_REASON_TIMER);
    int64_t after_guard = sdf_power_policy_get_wake_guard_until_us();
    
    TEST_ASSERT_GREATER_THAN_INT64(after_guard, before_guard);
}

void test_power_policy_is_ready(void) {
    TEST_ASSERT_FALSE(sdf_power_policy_is_ready());
    
    sdf_power_policy_init(&s_test_config);
    TEST_ASSERT_TRUE(sdf_power_policy_is_ready());
}

void test_power_policy_battery_percent(void) {
    sdf_power_policy_init(&s_test_config);
    
    uint8_t battery = sdf_power_policy_get_battery_percent();
    /* Initial value should be from mock callback default */
    TEST_ASSERT_GREATER_OR_EQUAL_UINT8(0, battery);
    TEST_ASSERT_LESS_OR_EQUAL_UINT8(100, battery);
}