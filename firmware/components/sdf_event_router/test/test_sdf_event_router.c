#include "unity.h"

#include "sdf_event_router.h"
#include "sdf_event_router_capacity.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static int s_test_event_count = 0;
static sdf_event_router_event_t s_last_event;

static void test_event_handler(void *ctx, const sdf_event_router_event_t *event)
{
    (void)ctx;
    s_test_event_count++;
    s_last_event = *event;
}

void test_sdf_event_router_init_returns_ok(void) {
    sdf_event_router_reset_for_test();
    esp_err_t err = sdf_event_router_init();
    TEST_ASSERT_EQUAL(ESP_OK, err);
}

void test_sdf_event_router_init_idempotent(void) {
    sdf_event_router_reset_for_test();
    TEST_ASSERT_EQUAL(ESP_OK, sdf_event_router_init());
    TEST_ASSERT_EQUAL(ESP_OK, sdf_event_router_init());
}

void test_sdf_event_router_start_before_init_fails(void) {
    sdf_event_router_reset_for_test();
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, sdf_event_router_start());
}

void test_sdf_event_router_start_idempotent(void) {
    sdf_event_router_reset_for_test();
    TEST_ASSERT_EQUAL(ESP_OK, sdf_event_router_init());
    TEST_ASSERT_EQUAL(ESP_OK, sdf_event_router_start());
    TEST_ASSERT_EQUAL(ESP_OK, sdf_event_router_start());
}

void test_sdf_event_router_subscribe_and_emit(void) {
    sdf_event_router_reset_for_test();
    TEST_ASSERT_EQUAL(ESP_OK, sdf_event_router_init());

    s_test_event_count = 0;

    esp_err_t err = sdf_event_router_subscribe(SDF_EVENT_ROUTER_BIOMETRIC_MATCH,
                                               SDF_EVENT_ROUTER_PRIO_NORMAL,
                                               test_event_handler, NULL);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    TEST_ASSERT_EQUAL(ESP_OK, sdf_event_router_start());

    sdf_event_router_event_t event = {
        .type = SDF_EVENT_ROUTER_BIOMETRIC_MATCH,
        .priority = SDF_EVENT_ROUTER_PRIO_NORMAL,
        .payload.biometric.user_id = 42,
        .payload.biometric.confidence = 85
    };

    err = sdf_event_router_emit(&event, SDF_EVENT_ROUTER_EMIT_TIMEOUT_DEFAULT_MS);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    vTaskDelay(pdMS_TO_TICKS(50));
    TEST_ASSERT_EQUAL(1, s_test_event_count);
    TEST_ASSERT_EQUAL(SDF_EVENT_ROUTER_BIOMETRIC_MATCH, s_last_event.type);
    TEST_ASSERT_EQUAL(42, s_last_event.payload.biometric.user_id);
}

void test_sdf_event_router_subscribe_after_start_fails(void) {
    sdf_event_router_reset_for_test();
    TEST_ASSERT_EQUAL(ESP_OK, sdf_event_router_init());
    TEST_ASSERT_EQUAL(ESP_OK, sdf_event_router_start());

    esp_err_t err = sdf_event_router_subscribe(SDF_EVENT_ROUTER_BIOMETRIC_MATCH,
                                               SDF_EVENT_ROUTER_PRIO_NORMAL,
                                               test_event_handler, NULL);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, err);
}

void test_sdf_event_router_subscribe_rejects_invalid_type_and_sentinels(void) {
    sdf_event_router_reset_for_test();
    TEST_ASSERT_EQUAL(ESP_OK, sdf_event_router_init());

    esp_err_t err = sdf_event_router_subscribe(SDF_EVENT_ROUTER_TYPE_COUNT,
                                               SDF_EVENT_ROUTER_PRIO_NORMAL,
                                               test_event_handler, NULL);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);

    err = sdf_event_router_subscribe(SDF_EVENT_ROUTER_INTERNAL_WAKE,
                                     SDF_EVENT_ROUTER_PRIO_NORMAL,
                                     test_event_handler, NULL);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);

    err = sdf_event_router_subscribe(SDF_EVENT_ROUTER_BIOMETRIC_MATCH,
                                     SDF_EVENT_ROUTER_PRIO_NORMAL,
                                     NULL, NULL);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);
}

void test_sdf_event_router_subscribe_pool_exhaustion_fails_start(void) {
    sdf_event_router_reset_for_test();
    TEST_ASSERT_EQUAL(ESP_OK, sdf_event_router_init());

    /* Fill pool to capacity */
    for (size_t i = 0; i < SDF_EVENT_ROUTER_SUBSCRIBER_CAPACITY; i++) {
        esp_err_t err = sdf_event_router_subscribe(SDF_EVENT_ROUTER_POWER_BATTERY,
                                                   SDF_EVENT_ROUTER_PRIO_LOW,
                                                   test_event_handler, NULL);
        TEST_ASSERT_EQUAL(ESP_OK, err);
    }

    /* Next subscription exceeds capacity */
    esp_err_t err = sdf_event_router_subscribe(SDF_EVENT_ROUTER_POWER_BATTERY,
                                               SDF_EVENT_ROUTER_PRIO_LOW,
                                               test_event_handler, NULL);
    TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, err);

    /* start() must fail because a registration was rejected */
    TEST_ASSERT_EQUAL(ESP_FAIL, sdf_event_router_start());
}

static int s_multi_counts[5] = {0};
static void multi_cb_0(void *ctx, const sdf_event_router_event_t *e) { (void)ctx; (void)e; s_multi_counts[0]++; }
static void multi_cb_1(void *ctx, const sdf_event_router_event_t *e) { (void)ctx; (void)e; s_multi_counts[1]++; }
static void multi_cb_2(void *ctx, const sdf_event_router_event_t *e) { (void)ctx; (void)e; s_multi_counts[2]++; }
static void multi_cb_3(void *ctx, const sdf_event_router_event_t *e) { (void)ctx; (void)e; s_multi_counts[3]++; }
static void multi_cb_4(void *ctx, const sdf_event_router_event_t *e) { (void)ctx; (void)e; s_multi_counts[4]++; }

void test_sdf_event_router_multiple_subscribers_invoked_no_truncation(void) {
    sdf_event_router_reset_for_test();
    TEST_ASSERT_EQUAL(ESP_OK, sdf_event_router_init());

    for (int i = 0; i < 5; i++) s_multi_counts[i] = 0;

    TEST_ASSERT_EQUAL(ESP_OK, sdf_event_router_subscribe(SDF_EVENT_ROUTER_BIOMETRIC_MATCH,
                                                         SDF_EVENT_ROUTER_PRIO_NORMAL, multi_cb_0, NULL));
    TEST_ASSERT_EQUAL(ESP_OK, sdf_event_router_subscribe(SDF_EVENT_ROUTER_BIOMETRIC_MATCH,
                                                         SDF_EVENT_ROUTER_PRIO_NORMAL, multi_cb_1, NULL));
    TEST_ASSERT_EQUAL(ESP_OK, sdf_event_router_subscribe(SDF_EVENT_ROUTER_BIOMETRIC_MATCH,
                                                         SDF_EVENT_ROUTER_PRIO_NORMAL, multi_cb_2, NULL));
    TEST_ASSERT_EQUAL(ESP_OK, sdf_event_router_subscribe(SDF_EVENT_ROUTER_BIOMETRIC_MATCH,
                                                         SDF_EVENT_ROUTER_PRIO_NORMAL, multi_cb_3, NULL));
    TEST_ASSERT_EQUAL(ESP_OK, sdf_event_router_subscribe(SDF_EVENT_ROUTER_BIOMETRIC_MATCH,
                                                         SDF_EVENT_ROUTER_PRIO_NORMAL, multi_cb_4, NULL));

    TEST_ASSERT_EQUAL(ESP_OK, sdf_event_router_start());

    sdf_event_router_event_t event = {
        .type = SDF_EVENT_ROUTER_BIOMETRIC_MATCH,
        .priority = SDF_EVENT_ROUTER_PRIO_NORMAL,
    };
    TEST_ASSERT_EQUAL(ESP_OK, sdf_event_router_emit(&event, SDF_EVENT_ROUTER_EMIT_TIMEOUT_DEFAULT_MS));

    vTaskDelay(pdMS_TO_TICKS(50));
    for (int i = 0; i < 5; i++) {
        TEST_ASSERT_EQUAL(1, s_multi_counts[i]);
    }
}

static int s_prio_low_sub_count = 0;
static int s_prio_crit_sub_count = 0;
static void prio_low_cb(void *ctx, const sdf_event_router_event_t *e) { (void)ctx; (void)e; s_prio_low_sub_count++; }
static void prio_crit_cb(void *ctx, const sdf_event_router_event_t *e) { (void)ctx; (void)e; s_prio_crit_sub_count++; }

void test_sdf_event_router_min_prio_filter_semantics(void) {
    sdf_event_router_reset_for_test();
    TEST_ASSERT_EQUAL(ESP_OK, sdf_event_router_init());

    s_prio_low_sub_count = 0;
    s_prio_crit_sub_count = 0;

    /* LOW minimum accepts CRITICAL, HIGH, NORMAL, LOW */
    TEST_ASSERT_EQUAL(ESP_OK, sdf_event_router_subscribe(SDF_EVENT_ROUTER_POWER_WAKE,
                                                         SDF_EVENT_ROUTER_PRIO_LOW,
                                                         prio_low_cb, NULL));
    /* CRITICAL minimum accepts ONLY CRITICAL */
    TEST_ASSERT_EQUAL(ESP_OK, sdf_event_router_subscribe(SDF_EVENT_ROUTER_POWER_WAKE,
                                                         SDF_EVENT_ROUTER_PRIO_CRITICAL,
                                                         prio_crit_cb, NULL));

    TEST_ASSERT_EQUAL(ESP_OK, sdf_event_router_start());

    /* Emit CRITICAL: both receive */
    sdf_event_router_event_t crit_evt = {
        .type = SDF_EVENT_ROUTER_POWER_WAKE,
        .priority = SDF_EVENT_ROUTER_PRIO_CRITICAL,
    };
    TEST_ASSERT_EQUAL(ESP_OK, sdf_event_router_emit(&crit_evt, SDF_EVENT_ROUTER_EMIT_TIMEOUT_DEFAULT_MS));
    vTaskDelay(pdMS_TO_TICKS(50));
    TEST_ASSERT_EQUAL(1, s_prio_low_sub_count);
    TEST_ASSERT_EQUAL(1, s_prio_crit_sub_count);

    /* Emit NORMAL: only LOW subscriber receives */
    sdf_event_router_event_t norm_evt = {
        .type = SDF_EVENT_ROUTER_POWER_WAKE,
        .priority = SDF_EVENT_ROUTER_PRIO_NORMAL,
    };
    TEST_ASSERT_EQUAL(ESP_OK, sdf_event_router_emit(&norm_evt, SDF_EVENT_ROUTER_EMIT_TIMEOUT_DEFAULT_MS));
    vTaskDelay(pdMS_TO_TICKS(50));
    TEST_ASSERT_EQUAL(2, s_prio_low_sub_count);
    TEST_ASSERT_EQUAL(1, s_prio_crit_sub_count);

    /* Emit LOW: only LOW subscriber receives */
    sdf_event_router_event_t low_evt = {
        .type = SDF_EVENT_ROUTER_POWER_WAKE,
        .priority = SDF_EVENT_ROUTER_PRIO_LOW,
    };
    TEST_ASSERT_EQUAL(ESP_OK, sdf_event_router_emit(&low_evt, SDF_EVENT_ROUTER_EMIT_TIMEOUT_DEFAULT_MS));
    vTaskDelay(pdMS_TO_TICKS(50));
    TEST_ASSERT_EQUAL(3, s_prio_low_sub_count);
    TEST_ASSERT_EQUAL(1, s_prio_crit_sub_count);
}

static int s_reentrant_nested_count = 0;
static void reentrant_match_cb(void *ctx, const sdf_event_router_event_t *e)
{
    (void)ctx;
    (void)e;
    s_reentrant_nested_count++;
}

static bool s_reentrant_dispatched_inline = false;

static void reentrant_btn_cb(void *ctx, const sdf_event_router_event_t *e)
{
    (void)ctx;
    (void)e;
    /* Reentrant CRITICAL emit during dispatch */
    sdf_event_router_event_t nested_evt = {
        .type = SDF_EVENT_ROUTER_BIOMETRIC_MATCH,
        .priority = SDF_EVENT_ROUTER_PRIO_CRITICAL,
        .payload.biometric.user_id = 99
    };
    sdf_event_router_emit(&nested_evt, SDF_EVENT_ROUTER_EMIT_TIMEOUT_DEFAULT_MS);

    /* The emitter here IS the router task, so it cannot preempt itself: the
     * nested event must still be queued when this callback returns. Under the
     * old synchronous path it would already have been dispatched. */
    s_reentrant_dispatched_inline = (s_reentrant_nested_count != 0);
}

void test_sdf_event_router_reentrant_critical_emit_no_deadlock(void) {
    sdf_event_router_reset_for_test();
    TEST_ASSERT_EQUAL(ESP_OK, sdf_event_router_init());

    s_reentrant_nested_count = 0;
    s_reentrant_dispatched_inline = false;

    TEST_ASSERT_EQUAL(ESP_OK, sdf_event_router_subscribe(SDF_EVENT_ROUTER_BUTTON_PRESS,
                                                         SDF_EVENT_ROUTER_PRIO_HIGH,
                                                         reentrant_btn_cb, NULL));
    TEST_ASSERT_EQUAL(ESP_OK, sdf_event_router_subscribe(SDF_EVENT_ROUTER_BIOMETRIC_MATCH,
                                                         SDF_EVENT_ROUTER_PRIO_CRITICAL,
                                                         reentrant_match_cb, NULL));

    TEST_ASSERT_EQUAL(ESP_OK, sdf_event_router_start());

    sdf_event_router_event_t btn_evt = {
        .type = SDF_EVENT_ROUTER_BUTTON_PRESS,
        .priority = SDF_EVENT_ROUTER_PRIO_HIGH,
    };
    TEST_ASSERT_EQUAL(ESP_OK, sdf_event_router_emit(&btn_evt, SDF_EVENT_ROUTER_EMIT_TIMEOUT_DEFAULT_MS));

    vTaskDelay(pdMS_TO_TICKS(50));
    TEST_ASSERT_FALSE(s_reentrant_dispatched_inline);
    TEST_ASSERT_EQUAL(1, s_reentrant_nested_count);
}

static int s_lockout_entered_count = 0;
static int s_lockout_cleared_count = 0;
static void app_lockout_cb(void *ctx, const sdf_event_router_event_t *event)
{
    (void)ctx;
    if (event->priority == SDF_EVENT_ROUTER_PRIO_CRITICAL) {
        s_lockout_entered_count++;
    } else if (event->priority == SDF_EVENT_ROUTER_PRIO_NORMAL) {
        s_lockout_cleared_count++;
    }
}

void test_sdf_event_router_security_lockout_pair_delivered(void) {
    sdf_event_router_reset_for_test();
    TEST_ASSERT_EQUAL(ESP_OK, sdf_event_router_init());

    s_lockout_entered_count = 0;
    s_lockout_cleared_count = 0;

    /* Registered exactly as sdf_app registers */
    TEST_ASSERT_EQUAL(ESP_OK, sdf_event_router_subscribe(SDF_EVENT_ROUTER_SECURITY_LOCKOUT,
                                                         SDF_EVENT_ROUTER_PRIO_NORMAL,
                                                         app_lockout_cb, NULL));

    TEST_ASSERT_EQUAL(ESP_OK, sdf_event_router_start());

    /* 1. Lockout entered (CRITICAL emission) */
    sdf_event_router_event_t entered_evt = {
        .type = SDF_EVENT_ROUTER_SECURITY_LOCKOUT,
        .priority = SDF_EVENT_ROUTER_PRIO_CRITICAL,
        .payload.security.failed_attempts = 5
    };
    TEST_ASSERT_EQUAL(ESP_OK, sdf_event_router_emit(&entered_evt, SDF_EVENT_ROUTER_EMIT_TIMEOUT_DEFAULT_MS));

    /* 2. Lockout cleared (NORMAL emission) */
    sdf_event_router_event_t cleared_evt = {
        .type = SDF_EVENT_ROUTER_SECURITY_LOCKOUT,
        .priority = SDF_EVENT_ROUTER_PRIO_NORMAL,
        .payload.security.failed_attempts = 0
    };
    TEST_ASSERT_EQUAL(ESP_OK, sdf_event_router_emit(&cleared_evt, SDF_EVENT_ROUTER_EMIT_TIMEOUT_DEFAULT_MS));

    vTaskDelay(pdMS_TO_TICKS(50));
    TEST_ASSERT_EQUAL(1, s_lockout_entered_count);
    TEST_ASSERT_EQUAL(1, s_lockout_cleared_count);
}

void test_sdf_event_router_emit_before_start_delivered_after_start(void) {
    sdf_event_router_reset_for_test();
    TEST_ASSERT_EQUAL(ESP_OK, sdf_event_router_init());

    s_test_event_count = 0;

    TEST_ASSERT_EQUAL(ESP_OK, sdf_event_router_subscribe(SDF_EVENT_ROUTER_BIOMETRIC_MATCH_FAILED,
                                                         SDF_EVENT_ROUTER_PRIO_HIGH,
                                                         test_event_handler, NULL));

    /* Emit non-critical event BEFORE start() */
    sdf_event_router_event_t evt = {
        .type = SDF_EVENT_ROUTER_BIOMETRIC_MATCH_FAILED,
        .priority = SDF_EVENT_ROUTER_PRIO_HIGH,
        .payload.security.failed_attempts = 2
    };
    TEST_ASSERT_EQUAL(ESP_OK, sdf_event_router_emit(&evt, SDF_EVENT_ROUTER_EMIT_TIMEOUT_DEFAULT_MS));

    /* Not yet delivered because dispatch task is not running */
    vTaskDelay(pdMS_TO_TICKS(20));
    TEST_ASSERT_EQUAL(0, s_test_event_count);

    /* Start router -> drains queue */
    TEST_ASSERT_EQUAL(ESP_OK, sdf_event_router_start());

    vTaskDelay(pdMS_TO_TICKS(50));
    TEST_ASSERT_EQUAL(1, s_test_event_count);
    TEST_ASSERT_EQUAL(SDF_EVENT_ROUTER_BIOMETRIC_MATCH_FAILED, s_last_event.type);
    TEST_ASSERT_EQUAL(2, s_last_event.payload.security.failed_attempts);
}

void test_sdf_event_router_emit_zero_timeout_delivers(void) {
    sdf_event_router_reset_for_test();
    TEST_ASSERT_EQUAL(ESP_OK, sdf_event_router_init());

    s_test_event_count = 0;

    esp_err_t err = sdf_event_router_subscribe(SDF_EVENT_ROUTER_BUTTON_PRESS,
                                               SDF_EVENT_ROUTER_PRIO_HIGH,
                                               test_event_handler, NULL);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    TEST_ASSERT_EQUAL(ESP_OK, sdf_event_router_start());

    sdf_event_router_event_t event = {
        .type = SDF_EVENT_ROUTER_BUTTON_PRESS,
        .priority = SDF_EVENT_ROUTER_PRIO_HIGH,
        .payload.button = {
            .press_type = SDF_EVENT_ROUTER_BUTTON_PRESS_SINGLE,
            .press_duration_ms = 0
        }
    };

    err = sdf_event_router_emit(&event, 0);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    vTaskDelay(pdMS_TO_TICKS(50));
    TEST_ASSERT_EQUAL(1, s_test_event_count);
    TEST_ASSERT_EQUAL(SDF_EVENT_ROUTER_BUTTON_PRESS, s_last_event.type);
    TEST_ASSERT_EQUAL(SDF_EVENT_ROUTER_BUTTON_PRESS_SINGLE, s_last_event.payload.button.press_type);
}

void test_sdf_event_router_emit_zero_timeout_null_args(void) {
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, sdf_event_router_emit(NULL, 0));
}

void test_sdf_event_router_emit_rejects_internal_wake_and_invalid_type(void) {
    sdf_event_router_reset_for_test();
    TEST_ASSERT_EQUAL(ESP_OK, sdf_event_router_init());

    sdf_event_router_event_t wake_event = {
        .type = SDF_EVENT_ROUTER_INTERNAL_WAKE,
        .priority = SDF_EVENT_ROUTER_PRIO_NORMAL,
    };
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, sdf_event_router_emit(&wake_event, SDF_EVENT_ROUTER_EMIT_TIMEOUT_DEFAULT_MS));

    sdf_event_router_event_t invalid_event = {
        .type = SDF_EVENT_ROUTER_TYPE_COUNT,
        .priority = SDF_EVENT_ROUTER_PRIO_NORMAL,
    };
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, sdf_event_router_emit(&invalid_event, SDF_EVENT_ROUTER_EMIT_TIMEOUT_DEFAULT_MS));
}

void test_sdf_event_router_emit_zero_timeout_rejects_internal_wake_and_invalid_type(void) {
    sdf_event_router_reset_for_test();
    TEST_ASSERT_EQUAL(ESP_OK, sdf_event_router_init());

    sdf_event_router_event_t wake_event = {
        .type = SDF_EVENT_ROUTER_INTERNAL_WAKE,
        .priority = SDF_EVENT_ROUTER_PRIO_NORMAL,
    };
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, sdf_event_router_emit(&wake_event, 0));

    sdf_event_router_event_t invalid_event = {
        .type = SDF_EVENT_ROUTER_TYPE_COUNT,
        .priority = SDF_EVENT_ROUTER_PRIO_NORMAL,
    };
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, sdf_event_router_emit(&invalid_event, 0));
}

/* ---- Dispatch context and ordering (post sync-dispatch removal) ---- */

static TaskHandle_t s_ctx_cb_task = NULL;
static void ctx_recording_cb(void *ctx, const sdf_event_router_event_t *e)
{
    (void)ctx;
    (void)e;
    s_ctx_cb_task = xTaskGetCurrentTaskHandle();
}

/* The invariant is which context runs the callback, not whether emit() has
 * returned by then: the router task runs at priority 5 and the emitting task
 * here at the main-task priority, so the router legitimately preempts inside
 * xQueueSendToFront(). Asserting on ordering would test the scheduler; this
 * asserts what the router actually guarantees. */
void test_sdf_event_router_critical_dispatched_on_router_task(void) {
    sdf_event_router_reset_for_test();
    TEST_ASSERT_EQUAL(ESP_OK, sdf_event_router_init());

    s_ctx_cb_task = NULL;
    TaskHandle_t emitter = xTaskGetCurrentTaskHandle();

    TEST_ASSERT_EQUAL(ESP_OK, sdf_event_router_subscribe(SDF_EVENT_ROUTER_SECURITY_LOCKOUT,
                                                         SDF_EVENT_ROUTER_PRIO_CRITICAL,
                                                         ctx_recording_cb, NULL));
    TEST_ASSERT_EQUAL(ESP_OK, sdf_event_router_start());

    sdf_event_router_event_t evt = {
        .type = SDF_EVENT_ROUTER_SECURITY_LOCKOUT,
        .priority = SDF_EVENT_ROUTER_PRIO_CRITICAL,
        .payload.security.failed_attempts = 5,
    };
    TEST_ASSERT_EQUAL(ESP_OK, sdf_event_router_emit(&evt, SDF_EVENT_ROUTER_EMIT_TIMEOUT_DEFAULT_MS));

    vTaskDelay(pdMS_TO_TICKS(50));
    TEST_ASSERT_NOT_NULL(s_ctx_cb_task);
    TEST_ASSERT_NOT_EQUAL(emitter, s_ctx_cb_task);
}

/* Non-critical emitters must not run callbacks on their stack either. */
void test_sdf_event_router_noncritical_dispatched_on_router_task(void) {
    sdf_event_router_reset_for_test();
    TEST_ASSERT_EQUAL(ESP_OK, sdf_event_router_init());

    s_ctx_cb_task = NULL;
    TaskHandle_t emitter = xTaskGetCurrentTaskHandle();

    TEST_ASSERT_EQUAL(ESP_OK, sdf_event_router_subscribe(SDF_EVENT_ROUTER_AUDIT,
                                                         SDF_EVENT_ROUTER_PRIO_NORMAL,
                                                         ctx_recording_cb, NULL));
    TEST_ASSERT_EQUAL(ESP_OK, sdf_event_router_start());

    sdf_event_router_event_t evt = {
        .type = SDF_EVENT_ROUTER_AUDIT,
        .priority = SDF_EVENT_ROUTER_PRIO_NORMAL,
    };
    TEST_ASSERT_EQUAL(ESP_OK, sdf_event_router_emit(&evt, SDF_EVENT_ROUTER_EMIT_TIMEOUT_DEFAULT_MS));

    vTaskDelay(pdMS_TO_TICKS(50));
    TEST_ASSERT_NOT_NULL(s_ctx_cb_task);
    TEST_ASSERT_NOT_EQUAL(emitter, s_ctx_cb_task);
}

#define ORDER_LOG_CAPACITY 8
static uint8_t s_order_log[ORDER_LOG_CAPACITY];
static int s_order_log_count = 0;

static void order_recording_cb(void *ctx, const sdf_event_router_event_t *e)
{
    (void)ctx;
    if (s_order_log_count < ORDER_LOG_CAPACITY) {
        s_order_log[s_order_log_count++] = (uint8_t)e->payload.security.user_id;
    }
}

/* Emitting before start() keeps the dispatch task from draining, so the queue
 * contents are observed in exactly the order the router will dispatch them -
 * no reliance on scheduler timing. */
void test_sdf_event_router_critical_dispatched_ahead_of_queued_noncritical(void) {
    sdf_event_router_reset_for_test();
    TEST_ASSERT_EQUAL(ESP_OK, sdf_event_router_init());

    s_order_log_count = 0;

    TEST_ASSERT_EQUAL(ESP_OK, sdf_event_router_subscribe(SDF_EVENT_ROUTER_SECURITY_LOCKOUT,
                                                         SDF_EVENT_ROUTER_PRIO_LOW,
                                                         order_recording_cb, NULL));

    sdf_event_router_event_t normal_evt = {
        .type = SDF_EVENT_ROUTER_SECURITY_LOCKOUT,
        .priority = SDF_EVENT_ROUTER_PRIO_NORMAL,
    };
    normal_evt.payload.security.user_id = 1;
    TEST_ASSERT_EQUAL(ESP_OK, sdf_event_router_emit(&normal_evt, SDF_EVENT_ROUTER_EMIT_TIMEOUT_DEFAULT_MS));
    normal_evt.payload.security.user_id = 2;
    TEST_ASSERT_EQUAL(ESP_OK, sdf_event_router_emit(&normal_evt, SDF_EVENT_ROUTER_EMIT_TIMEOUT_DEFAULT_MS));

    sdf_event_router_event_t crit_evt = {
        .type = SDF_EVENT_ROUTER_SECURITY_LOCKOUT,
        .priority = SDF_EVENT_ROUTER_PRIO_CRITICAL,
    };
    crit_evt.payload.security.user_id = 99;
    TEST_ASSERT_EQUAL(ESP_OK, sdf_event_router_emit(&crit_evt, SDF_EVENT_ROUTER_EMIT_TIMEOUT_DEFAULT_MS));

    TEST_ASSERT_EQUAL(ESP_OK, sdf_event_router_start());
    vTaskDelay(pdMS_TO_TICKS(50));

    TEST_ASSERT_EQUAL(3, s_order_log_count);
    TEST_ASSERT_EQUAL(99, s_order_log[0]); /* critical jumped the two queued events */
    TEST_ASSERT_EQUAL(1, s_order_log[1]);
    TEST_ASSERT_EQUAL(2, s_order_log[2]);
}

void test_sdf_event_router_noncritical_dispatched_in_arrival_order(void) {
    sdf_event_router_reset_for_test();
    TEST_ASSERT_EQUAL(ESP_OK, sdf_event_router_init());

    s_order_log_count = 0;

    TEST_ASSERT_EQUAL(ESP_OK, sdf_event_router_subscribe(SDF_EVENT_ROUTER_SECURITY_LOCKOUT,
                                                         SDF_EVENT_ROUTER_PRIO_LOW,
                                                         order_recording_cb, NULL));

    sdf_event_router_event_t evt = {
        .type = SDF_EVENT_ROUTER_SECURITY_LOCKOUT,
        .priority = SDF_EVENT_ROUTER_PRIO_NORMAL,
    };
    evt.payload.security.user_id = 11;
    TEST_ASSERT_EQUAL(ESP_OK, sdf_event_router_emit(&evt, SDF_EVENT_ROUTER_EMIT_TIMEOUT_DEFAULT_MS));
    evt.payload.security.user_id = 22;
    TEST_ASSERT_EQUAL(ESP_OK, sdf_event_router_emit(&evt, SDF_EVENT_ROUTER_EMIT_TIMEOUT_DEFAULT_MS));

    TEST_ASSERT_EQUAL(ESP_OK, sdf_event_router_start());
    vTaskDelay(pdMS_TO_TICKS(50));

    TEST_ASSERT_EQUAL(2, s_order_log_count);
    TEST_ASSERT_EQUAL(11, s_order_log[0]);
    TEST_ASSERT_EQUAL(22, s_order_log[1]);
}

void test_sdf_event_router_critical_emit_before_start_delivered_after_start(void) {
    sdf_event_router_reset_for_test();
    TEST_ASSERT_EQUAL(ESP_OK, sdf_event_router_init());

    s_test_event_count = 0;

    TEST_ASSERT_EQUAL(ESP_OK, sdf_event_router_subscribe(SDF_EVENT_ROUTER_SECURITY_LOCKOUT,
                                                         SDF_EVENT_ROUTER_PRIO_CRITICAL,
                                                         test_event_handler, NULL));

    sdf_event_router_event_t evt = {
        .type = SDF_EVENT_ROUTER_SECURITY_LOCKOUT,
        .priority = SDF_EVENT_ROUTER_PRIO_CRITICAL,
        .payload.security.failed_attempts = 5,
    };
    TEST_ASSERT_EQUAL(ESP_OK, sdf_event_router_emit(&evt, SDF_EVENT_ROUTER_EMIT_TIMEOUT_DEFAULT_MS));

    /* Previously a critical emit dispatched inline here; it is now queued. */
    vTaskDelay(pdMS_TO_TICKS(20));
    TEST_ASSERT_EQUAL(0, s_test_event_count);

    TEST_ASSERT_EQUAL(ESP_OK, sdf_event_router_start());
    vTaskDelay(pdMS_TO_TICKS(50));

    TEST_ASSERT_EQUAL(1, s_test_event_count);
    TEST_ASSERT_EQUAL(SDF_EVENT_ROUTER_SECURITY_LOCKOUT, s_last_event.type);
    TEST_ASSERT_EQUAL(5, s_last_event.payload.security.failed_attempts);
}

/* Critical events are no longer exempt from a full queue: they take a slot like
 * everything else, they just take it at the front. Fills the queue before
 * start() so nothing drains, without hardcoding the configured depth. */
void test_sdf_event_router_full_queue_drops_critical(void) {
    sdf_event_router_reset_for_test();
    TEST_ASSERT_EQUAL(ESP_OK, sdf_event_router_init());

    TEST_ASSERT_EQUAL(ESP_OK, sdf_event_router_subscribe(SDF_EVENT_ROUTER_AUDIT,
                                                         SDF_EVENT_ROUTER_PRIO_LOW,
                                                         test_event_handler, NULL));

    sdf_event_router_event_t filler = {
        .type = SDF_EVENT_ROUTER_AUDIT,
        .priority = SDF_EVENT_ROUTER_PRIO_LOW,
    };

    /* Bounded well above the configured max depth (sdf_config caps it at 64). */
    esp_err_t err = ESP_OK;
    int sent = 0;
    for (int i = 0; i < 256; i++) {
        err = sdf_event_router_emit(&filler, 0);
        if (err != ESP_OK) {
            break;
        }
        sent++;
    }
    TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, err);
    TEST_ASSERT_GREATER_THAN(0, sent);

    sdf_event_router_event_t crit_evt = {
        .type = SDF_EVENT_ROUTER_SECURITY_LOCKOUT,
        .priority = SDF_EVENT_ROUTER_PRIO_CRITICAL,
    };
    TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, sdf_event_router_emit(&crit_evt, 0));
}

void test_sdf_event_router_internal_wake_sentinel_is_zero_and_biometric_match_nonzero(void) {
    /* Verify SDF_EVENT_ROUTER_INTERNAL_WAKE is value 0 so zero-initialized events map to wake sentinel */
    TEST_ASSERT_EQUAL(0, SDF_EVENT_ROUTER_INTERNAL_WAKE);

    /* Verify SDF_EVENT_ROUTER_BIOMETRIC_MATCH is non-zero so zero-init cannot forge a match */
    TEST_ASSERT_NOT_EQUAL(0, SDF_EVENT_ROUTER_BIOMETRIC_MATCH);

    /* Verify zero-initialized event has type SDF_EVENT_ROUTER_INTERNAL_WAKE */
    sdf_event_router_event_t zero_evt = {0};
    TEST_ASSERT_EQUAL(SDF_EVENT_ROUTER_INTERNAL_WAKE, zero_evt.type);
    TEST_ASSERT_NOT_EQUAL(SDF_EVENT_ROUTER_BIOMETRIC_MATCH, zero_evt.type);
}