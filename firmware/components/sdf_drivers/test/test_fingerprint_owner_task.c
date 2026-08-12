/**
 * @file test_fingerprint_owner_task.c
 * @brief Exercises fingerprint.c's owner-task request/reply dispatch (see
 * openspec/changes/fp-io-owner-task).
 *
 * These tests run against the Linux host mock UART (sdf_drivers_mock_linux.c),
 * which never returns real sensor data, so every fp_* op here completes with
 * an error result. What's under test is the *dispatch plumbing* - that
 * fp_init() stands up a real owner task, that fp_* calls round-trip through
 * its request queue and FP_REPLY_NOTIFY_IDX notification instead of hanging
 * or timing out prematurely, and that concurrent callers are serialized
 * rather than racing or bailing out on a stale lock-wait deadline.
 */
#include "fingerprint.h"
#include "fingerprint_testable.h"
#include "sdf_mock_linux_drivers.h"
#include "unity.h"

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

/* Arbitrary but in-range values - fp_config_valid() only checks bounds; the
 * mock UART/GPIO never touch real hardware. */
static sdf_fingerprint_driver_config_t fp_test_config(void) {
  sdf_fingerprint_driver_config_t config = {
      .uart_port = 0,
      .tx_pin = 0,
      .rx_pin = 1,
      .power_en_pin = 2,
      .baud_rate = 115200,
      .response_timeout_ms = 5000,
      .rx_buffer_size = 256,
      .tx_buffer_size = 256,
  };
  return config;
}

static void fp_owner_task_test_teardown(void) {
  fp_deinit();
  sdf_mock_uart_set_read_delay_ms(0);
}

/* ---------- 5.1: fp_init()/fp_deinit() stand up and tear down the owner
 * task, and a routed fp_* call round-trips through it instead of hanging. */

void test_fp_owner_task_dispatch_round_trip(void) {
  sdf_fingerprint_driver_config_t config = fp_test_config();
  TEST_ASSERT_FALSE(fp_is_ready());
  TEST_ASSERT_EQUAL(ESP_OK, fp_init(&config));
  TEST_ASSERT_TRUE(fp_is_ready());

  /* Idempotent re-init must not spin up a second owner task/queue. */
  TEST_ASSERT_EQUAL(ESP_OK, fp_init(&config));

  sdf_fingerprint_match_t match = {0};
  sdf_fingerprint_op_result_t result = fp_match_1n(&match);
  /* The mock UART never produces a valid frame, so this can't succeed - the
   * point is that it returns at all (the request made it to the owner task
   * and back) rather than hanging forever. */
  TEST_ASSERT_NOT_EQUAL(SDF_FINGERPRINT_OP_OK, result);

  fp_deinit();
  TEST_ASSERT_FALSE(fp_is_ready());

  /* fp_deinit() drains the owner task via FP_OP_STOP; calling it again once
   * already torn down must be a safe no-op. */
  fp_deinit();
}

/* ---------- shared helper for the concurrency tests below ---------- */

typedef struct {
  SemaphoreHandle_t done;
  int64_t start_us;
  int64_t end_us;
  sdf_fingerprint_op_result_t result;
} fp_match_task_ctx_t;

static void fp_match_task_fn(void *arg) {
  fp_match_task_ctx_t *ctx = (fp_match_task_ctx_t *)arg;
  sdf_fingerprint_match_t match = {0};
  ctx->start_us = esp_timer_get_time();
  ctx->result = fp_match_1n(&match);
  ctx->end_us = esp_timer_get_time();
  xSemaphoreGive(ctx->done);
  vTaskDelete(NULL);
}

/* Op duration used by the tests below - comfortably past the old
 * FP_MUTEX_WAIT_MS=250 that used to make a second caller bail out early. */
#define FP_TEST_OP_DELAY_MS 400u

/* ---------- 5.2: two overlapping requests from different tasks ---------- */

void test_fp_owner_task_serializes_concurrent_requests(void) {
  sdf_fingerprint_driver_config_t config = fp_test_config();
  TEST_ASSERT_EQUAL(ESP_OK, fp_init(&config));
  sdf_mock_uart_set_read_delay_ms(FP_TEST_OP_DELAY_MS);

  fp_match_task_ctx_t ctx_a = {.done = xSemaphoreCreateBinary()};
  TEST_ASSERT_NOT_NULL(ctx_a.done);

  xTaskCreate(fp_match_task_fn, "fp_test_a", 4096, &ctx_a, 5, NULL);
  /* Give task A's request time to reach the owner task and start being
   * processed (i.e. block inside the mocked UART read) before task B's
   * request lands behind it in the queue. */
  vTaskDelay(pdMS_TO_TICKS(50));

  sdf_fingerprint_match_t match_b = {0};
  int64_t start_b_us = esp_timer_get_time();
  sdf_fingerprint_op_result_t result_b = fp_match_1n(&match_b);
  int64_t end_b_us = esp_timer_get_time();

  TEST_ASSERT_EQUAL(pdTRUE, xSemaphoreTake(ctx_a.done, pdMS_TO_TICKS(5000)));
  vSemaphoreDelete(ctx_a.done);

  /* Both ops still fail (the mock never returns real sensor data), but
   * neither should have bailed out on a stale lock-wait deadline: caller B
   * only got its turn - and its own op only finished - well past the old
   * 250ms mutex-wait ceiling, proving the owner-task queue actually
   * serialized the two requests instead of one of them timing out. */
  int64_t elapsed_b_ms = (end_b_us - start_b_us) / 1000;
  TEST_ASSERT_GREATER_OR_EQUAL_INT64(300, elapsed_b_ms);
  TEST_ASSERT_NOT_EQUAL(SDF_FINGERPRINT_OP_OK, result_b);
  TEST_ASSERT_NOT_EQUAL(SDF_FINGERPRINT_OP_OK, ctx_a.result);

  fp_owner_task_test_teardown();
}

/* ---------- 5.3: power-off request deferred until in-flight op completes */

void test_fp_owner_task_power_off_deferred_until_op_completes(void) {
  sdf_fingerprint_driver_config_t config = fp_test_config();
  TEST_ASSERT_EQUAL(ESP_OK, fp_init(&config));

  /* fp_set_power() is itself routed through the owner task - use it to put
   * the sensor in a known "powered on" state before starting the in-flight
   * op below. */
  fp_set_power(true);
  TEST_ASSERT_TRUE(fp_test_get_last_power_enable_level());

  sdf_mock_uart_set_read_delay_ms(FP_TEST_OP_DELAY_MS);

  fp_match_task_ctx_t ctx = {.done = xSemaphoreCreateBinary()};
  TEST_ASSERT_NOT_NULL(ctx.done);
  xTaskCreate(fp_match_task_fn, "fp_test_inflight", 4096, &ctx, 5, NULL);
  vTaskDelay(pdMS_TO_TICKS(50)); /* let the in-flight op start */

  int64_t power_off_start_us = esp_timer_get_time();
  fp_set_power(false);
  int64_t power_off_end_us = esp_timer_get_time();

  TEST_ASSERT_EQUAL(pdTRUE, xSemaphoreTake(ctx.done, pdMS_TO_TICKS(5000)));
  vSemaphoreDelete(ctx.done);

  /* fp_set_power(false) must not have returned - and the power-enable pin
   * must not have been dropped - until the in-flight match op had already
   * finished, i.e. the power change was deferred rather than applied
   * mid-transaction. */
  TEST_ASSERT_TRUE(power_off_end_us >= ctx.end_us);
  int64_t power_off_elapsed_ms = (power_off_end_us - power_off_start_us) / 1000;
  TEST_ASSERT_GREATER_OR_EQUAL_INT64(300, power_off_elapsed_ms);
  TEST_ASSERT_FALSE(fp_test_get_last_power_enable_level());

  fp_owner_task_test_teardown();
}
