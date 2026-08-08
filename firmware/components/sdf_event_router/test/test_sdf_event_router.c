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