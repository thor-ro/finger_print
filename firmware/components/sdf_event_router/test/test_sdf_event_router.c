#include "unity.h"

#include "sdf_event_router.h"
#include "sdf_event_router_capacity.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

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
static esp_err_t s_reentrant_emit_err = ESP_OK;

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
    s_reentrant_emit_err = sdf_event_router_emit(&nested_evt,
                                                 SDF_EVENT_ROUTER_EMIT_TIMEOUT_DEFAULT_MS);

    /* Nothing may be dispatched inline either way; asserted after the call so
     * a future regression that reintroduces synchronous dispatch is caught
     * here rather than only by the return code. */
    s_reentrant_dispatched_inline = (s_reentrant_nested_count != 0);
}

/* The emit is issued from the router task mid-dispatch, so it is rejected
 * outright: waiting for queue space would be waiting on the only task that can
 * drain the queue. This previously asserted the nested event was queued and
 * later dispatched - a guarantee that never held on a full queue. */
void test_sdf_event_router_reentrant_critical_emit_rejected(void) {
    sdf_event_router_reset_for_test();
    TEST_ASSERT_EQUAL(ESP_OK, sdf_event_router_init());

    s_reentrant_nested_count = 0;
    s_reentrant_dispatched_inline = false;
    s_reentrant_emit_err = ESP_OK;

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
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, s_reentrant_emit_err);
    TEST_ASSERT_FALSE(s_reentrant_dispatched_inline);
    /* Neither enqueued nor dispatched: the count stays at zero forever, not
     * just until the callback returns. */
    TEST_ASSERT_EQUAL(0, s_reentrant_nested_count);
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
/* ---- Emit from dispatch is rejected (design.md - D6) -------------------- */

#define EMIT_BAN_ATTEMPTS 4

static esp_err_t s_emit_ban_results[EMIT_BAN_ATTEMPTS];
static int s_emit_ban_invocations = 0;
static int s_emit_ban_target_count = 0;

static void emit_ban_target_cb(void *ctx, const sdf_event_router_event_t *e)
{
    (void)ctx;
    (void)e;
    s_emit_ban_target_count++;
}

static void emit_ban_btn_cb(void *ctx, const sdf_event_router_event_t *e)
{
    (void)ctx;
    (void)e;
    if (++s_emit_ban_invocations != 1) {
        return;
    }

    /* Every combination of priority and send timeout that a subscriber could
     * plausibly reach for. All four are rejected before the timeout is ever
     * consulted, so none of them can stall the dispatch task. */
    sdf_event_router_event_t crit = {
        .type = SDF_EVENT_ROUTER_BIOMETRIC_MATCH,
        .priority = SDF_EVENT_ROUTER_PRIO_CRITICAL,
    };
    sdf_event_router_event_t normal = {
        .type = SDF_EVENT_ROUTER_BIOMETRIC_MATCH,
        .priority = SDF_EVENT_ROUTER_PRIO_NORMAL,
    };
    s_emit_ban_results[0] = sdf_event_router_emit(&crit, 0);
    s_emit_ban_results[1] = sdf_event_router_emit(&crit, SDF_EVENT_ROUTER_EMIT_TIMEOUT_DEFAULT_MS);
    s_emit_ban_results[2] = sdf_event_router_emit(&normal, 0);
    s_emit_ban_results[3] = sdf_event_router_emit(&normal, SDF_EVENT_ROUTER_EMIT_TIMEOUT_DEFAULT_MS);
}

void test_sdf_event_router_emit_from_callback_rejected_all_priorities_and_timeouts(void) {
    sdf_event_router_reset_for_test();
    TEST_ASSERT_EQUAL(ESP_OK, sdf_event_router_init());

    s_emit_ban_invocations = 0;
    s_emit_ban_target_count = 0;
    for (int i = 0; i < EMIT_BAN_ATTEMPTS; i++) {
        s_emit_ban_results[i] = ESP_OK;
    }

    TEST_ASSERT_EQUAL(ESP_OK, sdf_event_router_subscribe(SDF_EVENT_ROUTER_BUTTON_PRESS,
                                                         SDF_EVENT_ROUTER_PRIO_LOW,
                                                         emit_ban_btn_cb, NULL));
    TEST_ASSERT_EQUAL(ESP_OK, sdf_event_router_subscribe(SDF_EVENT_ROUTER_BIOMETRIC_MATCH,
                                                         SDF_EVENT_ROUTER_PRIO_LOW,
                                                         emit_ban_target_cb, NULL));
    TEST_ASSERT_EQUAL(ESP_OK, sdf_event_router_start());

    sdf_event_router_event_t btn = {
        .type = SDF_EVENT_ROUTER_BUTTON_PRESS,
        .priority = SDF_EVENT_ROUTER_PRIO_HIGH,
    };
    TEST_ASSERT_EQUAL(ESP_OK, sdf_event_router_emit(&btn, SDF_EVENT_ROUTER_EMIT_TIMEOUT_DEFAULT_MS));

    vTaskDelay(pdMS_TO_TICKS(100));

    TEST_ASSERT_EQUAL(1, s_emit_ban_invocations);
    for (int i = 0; i < EMIT_BAN_ATTEMPTS; i++) {
        TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, s_emit_ban_results[i]);
    }
    /* Rejected means not enqueued: no attempt is dispatched later either. */
    TEST_ASSERT_EQUAL(0, s_emit_ban_target_count);
}

/* Guards against getting the rejection condition wrong in the other direction:
 * `in_dispatch` alone, without the task check, would reject this. */
static volatile bool s_scope_cb_entered = false;
static volatile bool s_scope_cb_release = false;
static int s_scope_other_count = 0;

static void scope_stall_cb(void *ctx, const sdf_event_router_event_t *e)
{
    (void)ctx;
    (void)e;
    s_scope_cb_entered = true;
    /* Bounded so a missing release fails an assertion rather than wedging the
     * suite; vTaskDelay rather than a spin so the test task gets to run. */
    for (int i = 0; i < 2000 && !s_scope_cb_release; i++) {
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

static void scope_other_cb(void *ctx, const sdf_event_router_event_t *e)
{
    (void)ctx;
    (void)e;
    s_scope_other_count++;
}

void test_sdf_event_router_emit_from_other_task_accepted_during_dispatch(void) {
    sdf_event_router_reset_for_test();
    TEST_ASSERT_EQUAL(ESP_OK, sdf_event_router_init());

    s_scope_cb_entered = false;
    s_scope_cb_release = false;
    s_scope_other_count = 0;

    TEST_ASSERT_EQUAL(ESP_OK, sdf_event_router_subscribe(SDF_EVENT_ROUTER_BUTTON_PRESS,
                                                         SDF_EVENT_ROUTER_PRIO_LOW,
                                                         scope_stall_cb, NULL));
    TEST_ASSERT_EQUAL(ESP_OK, sdf_event_router_subscribe(SDF_EVENT_ROUTER_AUDIT,
                                                         SDF_EVENT_ROUTER_PRIO_LOW,
                                                         scope_other_cb, NULL));
    TEST_ASSERT_EQUAL(ESP_OK, sdf_event_router_start());

    sdf_event_router_event_t btn = {
        .type = SDF_EVENT_ROUTER_BUTTON_PRESS,
        .priority = SDF_EVENT_ROUTER_PRIO_HIGH,
    };
    TEST_ASSERT_EQUAL(ESP_OK, sdf_event_router_emit(&btn, SDF_EVENT_ROUTER_EMIT_TIMEOUT_DEFAULT_MS));

    for (int i = 0; i < 500 && !s_scope_cb_entered; i++) {
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    TEST_ASSERT_TRUE_MESSAGE(s_scope_cb_entered, "dispatch never started");

    /* This test task is not the dispatch task, so its emit is accepted even
     * though a dispatch is demonstrably in progress right now. */
    sdf_event_router_event_t audit = {
        .type = SDF_EVENT_ROUTER_AUDIT,
        .priority = SDF_EVENT_ROUTER_PRIO_LOW,
    };
    TEST_ASSERT_EQUAL(ESP_OK, sdf_event_router_emit(&audit, SDF_EVENT_ROUTER_EMIT_TIMEOUT_DEFAULT_MS));

    s_scope_cb_release = true;
    vTaskDelay(pdMS_TO_TICKS(100));
    TEST_ASSERT_EQUAL(1, s_scope_other_count);
}

/* A full queue is the case the ban exists for: the old blocking send would
 * wait out the whole timeout for space only this task can free. Asserts on
 * elapsed time, because the return code alone would also pass if the emit had
 * blocked for the full 2000 ms and then failed. */
static volatile bool s_stall_cb_entered = false;
static volatile bool s_stall_queue_refilled = false;
static volatile int s_stall_invocations = 0;
static volatile esp_err_t s_stall_emit_err = ESP_OK;
static volatile int64_t s_stall_emit_us = 0;

#define STALL_EMIT_TIMEOUT_MS 2000

static void stall_full_queue_cb(void *ctx, const sdf_event_router_event_t *e)
{
    (void)ctx;
    (void)e;
    if (++s_stall_invocations != 1) {
        return;
    }

    s_stall_cb_entered = true;
    /* Receiving this event freed one queue slot. Wait for the test task to
     * take it back so the emit below meets a genuinely full queue. */
    for (int i = 0; i < 2000 && !s_stall_queue_refilled; i++) {
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    sdf_event_router_event_t nested = {
        .type = SDF_EVENT_ROUTER_AUDIT,
        .priority = SDF_EVENT_ROUTER_PRIO_LOW,
    };
    int64_t started_us = esp_timer_get_time();
    s_stall_emit_err = sdf_event_router_emit(&nested, STALL_EMIT_TIMEOUT_MS);
    s_stall_emit_us = esp_timer_get_time() - started_us;
}

void test_sdf_event_router_emit_from_callback_returns_promptly_on_full_queue(void) {
    sdf_event_router_reset_for_test();
    TEST_ASSERT_EQUAL(ESP_OK, sdf_event_router_init());

    s_stall_cb_entered = false;
    s_stall_queue_refilled = false;
    s_stall_invocations = 0;
    s_stall_emit_err = ESP_OK;
    s_stall_emit_us = 0;

    TEST_ASSERT_EQUAL(ESP_OK, sdf_event_router_subscribe(SDF_EVENT_ROUTER_AUDIT,
                                                         SDF_EVENT_ROUTER_PRIO_LOW,
                                                         stall_full_queue_cb, NULL));

    sdf_event_router_event_t filler = {
        .type = SDF_EVENT_ROUTER_AUDIT,
        .priority = SDF_EVENT_ROUTER_PRIO_LOW,
    };

    /* Fill before start() so nothing drains, without hardcoding the depth. */
    esp_err_t err = ESP_OK;
    for (int i = 0; i < 256; i++) {
        err = sdf_event_router_emit(&filler, 0);
        if (err != ESP_OK) {
            break;
        }
    }
    TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, err);

    TEST_ASSERT_EQUAL(ESP_OK, sdf_event_router_start());

    for (int i = 0; i < 500 && !s_stall_cb_entered; i++) {
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    TEST_ASSERT_TRUE_MESSAGE(s_stall_cb_entered, "dispatch never started");

    /* Refill the slot the dispatch freed. Emitted from the test task, so it is
     * accepted; it stops at ESP_ERR_NO_MEM, which is the state we want. */
    for (int i = 0; i < 256; i++) {
        if (sdf_event_router_emit(&filler, 0) != ESP_OK) {
            break;
        }
    }
    s_stall_queue_refilled = true;

    for (int i = 0; i < 1000 && s_stall_emit_us == 0; i++) {
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, s_stall_emit_err);
    TEST_ASSERT_GREATER_THAN(0, (int)s_stall_emit_us);
    /* An order of magnitude below the requested timeout: the emit returned
     * without waiting for space, not after giving up on it. */
    TEST_ASSERT_LESS_THAN_MESSAGE((int)(STALL_EMIT_TIMEOUT_MS * 1000 / 10),
                                  (int)s_stall_emit_us,
                                  "emit from dispatch waited for queue space");
}
