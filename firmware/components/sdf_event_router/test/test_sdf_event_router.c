#include "unity.h"

#include "sdf_event_router.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static int s_test_event_count = 0;
static sdf_event_router_event_t s_last_event;
static sdf_event_router_subscriber_t *s_test_handle = NULL;

static void test_event_handler(void *ctx, const sdf_event_router_event_t *event)
{
    (void)ctx;
    s_test_event_count++;
    s_last_event = *event;
}

void test_sdf_event_router_init_returns_ok(void) {
    esp_err_t err = sdf_event_router_init();
    TEST_ASSERT_EQUAL(ESP_OK, err);
}

void test_sdf_event_router_init_idempotent(void) {
    TEST_ASSERT_EQUAL(ESP_OK, sdf_event_router_init());
    TEST_ASSERT_EQUAL(ESP_OK, sdf_event_router_init());
}

void test_sdf_event_router_subscribe_and_emit(void) {
    s_test_event_count = 0;
    s_test_handle = NULL;

    esp_err_t err = sdf_event_router_subscribe(SDF_EVENT_ROUTER_BIOMETRIC_MATCH,
                                               SDF_EVENT_ROUTER_PRIO_NORMAL,
                                               test_event_handler, NULL, &s_test_handle);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_NOT_NULL(s_test_handle);

    sdf_event_router_event_t event = {
        .type = SDF_EVENT_ROUTER_BIOMETRIC_MATCH,
        .priority = SDF_EVENT_ROUTER_PRIO_NORMAL,
        .payload.biometric.user_id = 42,
        .payload.biometric.confidence = 85
    };

    err = sdf_event_router_emit(&event);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    vTaskDelay(pdMS_TO_TICKS(50));
    TEST_ASSERT_EQUAL(1, s_test_event_count);
    TEST_ASSERT_EQUAL(SDF_EVENT_ROUTER_BIOMETRIC_MATCH, s_last_event.type);
    TEST_ASSERT_EQUAL(42, s_last_event.payload.biometric.user_id);
}

void test_sdf_event_router_subscribe_rejects_invalid_type(void) {
    sdf_event_router_subscriber_t *handle = NULL;

    esp_err_t err = sdf_event_router_subscribe(SDF_EVENT_ROUTER_TYPE_COUNT,
                                               SDF_EVENT_ROUTER_PRIO_NORMAL,
                                               test_event_handler, NULL, &handle);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);
    TEST_ASSERT_NULL(handle);

    err = sdf_event_router_subscribe(SDF_EVENT_ROUTER_INTERNAL_WAKE,
                                     SDF_EVENT_ROUTER_PRIO_NORMAL,
                                     test_event_handler, NULL, &handle);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);
    TEST_ASSERT_NULL(handle);
}

void test_sdf_event_router_unsubscribe(void) {
    if (s_test_handle == NULL) {
        TEST_FAIL_MESSAGE("Previous test must run first to set handle");
    }

    s_test_event_count = 0;

    sdf_event_router_event_t event = {
        .type = SDF_EVENT_ROUTER_BIOMETRIC_MATCH,
        .priority = SDF_EVENT_ROUTER_PRIO_NORMAL
    };

    esp_err_t err = sdf_event_router_emit(&event);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    vTaskDelay(pdMS_TO_TICKS(50));
    TEST_ASSERT_EQUAL(1, s_test_event_count);

    err = sdf_event_router_unsubscribe(s_test_handle);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    s_test_handle = NULL;

    s_test_event_count = 0;
    err = sdf_event_router_emit(&event);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    vTaskDelay(pdMS_TO_TICKS(50));
    TEST_ASSERT_EQUAL(0, s_test_event_count);
}

void test_sdf_event_router_emit_nonblocking_delivers(void) {
    s_test_event_count = 0;
    s_test_handle = NULL;

    esp_err_t err = sdf_event_router_subscribe(SDF_EVENT_ROUTER_BUTTON_PRESS,
                                               SDF_EVENT_ROUTER_PRIO_HIGH,
                                               test_event_handler, NULL, &s_test_handle);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_NOT_NULL(s_test_handle);

    sdf_event_router_event_t event = {
        .type = SDF_EVENT_ROUTER_BUTTON_PRESS,
        .priority = SDF_EVENT_ROUTER_PRIO_HIGH,
        .payload.button = {
            .press_type = SDF_EVENT_ROUTER_BUTTON_PRESS_SINGLE,
            .press_duration_ms = 0
        }
    };

    err = sdf_event_router_emit_nonblocking(&event);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    vTaskDelay(pdMS_TO_TICKS(50));
    TEST_ASSERT_EQUAL(1, s_test_event_count);
    TEST_ASSERT_EQUAL(SDF_EVENT_ROUTER_BUTTON_PRESS, s_last_event.type);
    TEST_ASSERT_EQUAL(SDF_EVENT_ROUTER_BUTTON_PRESS_SINGLE, s_last_event.payload.button.press_type);

    err = sdf_event_router_unsubscribe(s_test_handle);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    s_test_handle = NULL;
}

void test_sdf_event_router_emit_nonblocking_null_args(void) {
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, sdf_event_router_emit_nonblocking(NULL));
}

void test_sdf_event_router_emit_rejects_internal_wake_and_invalid_type(void) {
    sdf_event_router_event_t wake_event = {
        .type = SDF_EVENT_ROUTER_INTERNAL_WAKE,
        .priority = SDF_EVENT_ROUTER_PRIO_NORMAL,
    };
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, sdf_event_router_emit(&wake_event));

    sdf_event_router_event_t invalid_event = {
        .type = SDF_EVENT_ROUTER_TYPE_COUNT,
        .priority = SDF_EVENT_ROUTER_PRIO_NORMAL,
    };
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, sdf_event_router_emit(&invalid_event));
}

void test_sdf_event_router_emit_nonblocking_rejects_internal_wake_and_invalid_type(void) {
    sdf_event_router_event_t wake_event = {
        .type = SDF_EVENT_ROUTER_INTERNAL_WAKE,
        .priority = SDF_EVENT_ROUTER_PRIO_NORMAL,
    };
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, sdf_event_router_emit_nonblocking(&wake_event));

    sdf_event_router_event_t invalid_event = {
        .type = SDF_EVENT_ROUTER_TYPE_COUNT,
        .priority = SDF_EVENT_ROUTER_PRIO_NORMAL,
    };
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, sdf_event_router_emit_nonblocking(&invalid_event));
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