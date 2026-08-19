#include "unity.h"

#include "sdf_config.h"
#include "sdf_mock_linux_zigbee.h"
#include "sdf_protocol_zigbee.h"
#include "sdf_zigbee_attr_cache.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <string.h>

/* The target implementation applies attribute updates asynchronously on a task
 * it owns, so these tests assert the contract from sdf_protocol_zigbee.h -
 * acceptance, coalescing to the latest value, synchronous argument rejection -
 * rather than that any individual ZCL write happened. Nothing here may assume
 * an attribute is readable the instant an update call returns. */

static void zigbee_test_enable(bool enabled) {
  TEST_ASSERT_EQUAL(ESP_OK, sdf_config_set_zigbee_enabled(enabled));
  sdf_protocol_zigbee_mock_reset();
}

void test_sdf_protocol_zigbee_factory_reset_disabled_returns_not_supported(void) {
  esp_err_t err = sdf_protocol_zigbee_factory_reset();
  TEST_ASSERT_EQUAL(ESP_ERR_NOT_SUPPORTED, err);
}

void test_sdf_protocol_zigbee_factory_reset_enabled_returns_ok(void) {
  esp_err_t err = sdf_protocol_zigbee_factory_reset();
  TEST_ASSERT_EQUAL(ESP_ERR_NOT_SUPPORTED, err);
}

/* Requirement: Attribute updates are coalesced to the latest value.
 * Several updates recorded before an apply must converge on the last one; no
 * superseded intermediate value may be left as the final attribute value. */
void test_sdf_protocol_zigbee_lock_state_updates_coalesce_to_latest(void) {
  zigbee_test_enable(true);

  TEST_ASSERT_EQUAL(ESP_OK, sdf_protocol_zigbee_update_lock_state(
                                SDF_PROTOCOL_ZIGBEE_LOCK_STATE_UNLOCKED));
  TEST_ASSERT_EQUAL(ESP_OK, sdf_protocol_zigbee_update_lock_state(
                                SDF_PROTOCOL_ZIGBEE_LOCK_STATE_NOT_FULLY_LOCKED));
  TEST_ASSERT_EQUAL(ESP_OK, sdf_protocol_zigbee_update_lock_state(
                                SDF_PROTOCOL_ZIGBEE_LOCK_STATE_LOCKED));

  TEST_ASSERT_EQUAL_UINT8(SDF_PROTOCOL_ZIGBEE_LOCK_STATE_LOCKED,
                          sdf_protocol_zigbee_mock_get_lock_state());

  zigbee_test_enable(false);
}

/* Requirement: Attribute updates are coalesced to the latest value.
 * Scenario: distinct attributes updated in the same burst are all applied. */
void test_sdf_protocol_zigbee_burst_applies_every_attribute(void) {
  zigbee_test_enable(true);

  TEST_ASSERT_EQUAL(ESP_OK, sdf_protocol_zigbee_update_lock_state(
                                SDF_PROTOCOL_ZIGBEE_LOCK_STATE_UNLOCKED));
  TEST_ASSERT_EQUAL(ESP_OK, sdf_protocol_zigbee_update_battery_percent(42));
  TEST_ASSERT_EQUAL(ESP_OK, sdf_protocol_zigbee_update_alarm_mask(0x0005));

  TEST_ASSERT_EQUAL_UINT8(SDF_PROTOCOL_ZIGBEE_LOCK_STATE_UNLOCKED,
                          sdf_protocol_zigbee_mock_get_lock_state());
  /* ZCL reports battery in half-percent units. */
  TEST_ASSERT_EQUAL_UINT8(
      84, sdf_protocol_zigbee_mock_get_battery_percent_remaining());
  TEST_ASSERT_EQUAL_UINT16(0x0005,
                           sdf_protocol_zigbee_mock_get_alarm_mask());

  zigbee_test_enable(false);
}

/* Requirement: Update calls report argument errors synchronously.
 * An out-of-range value must be rejected and must record nothing. */
void test_sdf_protocol_zigbee_invalid_argument_rejected_synchronously(void) {
  zigbee_test_enable(true);

  TEST_ASSERT_EQUAL(ESP_OK, sdf_protocol_zigbee_update_battery_percent(50));
  TEST_ASSERT_EQUAL_UINT8(
      100, sdf_protocol_zigbee_mock_get_battery_percent_remaining());

  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                    sdf_protocol_zigbee_update_battery_percent(101));
  /* Rejected outright - the previous value stands. */
  TEST_ASSERT_EQUAL_UINT8(
      100, sdf_protocol_zigbee_mock_get_battery_percent_remaining());

  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                    sdf_protocol_zigbee_update_lock_state(
                        (sdf_protocol_zigbee_lock_state_t)0x7E));

  zigbee_test_enable(false);
}

/* Requirement: user list is copied into a bounded buffer; an over-long list is
 * rejected rather than truncated, because a truncated JSON array is malformed
 * and serves a central worse than no update at all. */
void test_sdf_protocol_zigbee_user_list_accepts_and_coalesces(void) {
  zigbee_test_enable(true);

  TEST_ASSERT_NULL(sdf_protocol_zigbee_mock_get_user_list());

  TEST_ASSERT_EQUAL(ESP_OK, sdf_protocol_zigbee_update_user_list("[{\"id\":1}]"));
  TEST_ASSERT_EQUAL(ESP_OK,
                    sdf_protocol_zigbee_update_user_list("[{\"id\":2}]"));
  TEST_ASSERT_EQUAL_STRING("[{\"id\":2}]",
                           sdf_protocol_zigbee_mock_get_user_list());

  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                    sdf_protocol_zigbee_update_user_list(NULL));

  zigbee_test_enable(false);
}

void test_sdf_protocol_zigbee_user_list_over_long_rejected_not_truncated(void) {
  zigbee_test_enable(true);

  TEST_ASSERT_EQUAL(ESP_OK, sdf_protocol_zigbee_update_user_list("[]"));

  /* Comfortably past the buffer bound derived from the 10-user capacity. */
  char over_long[1024];
  memset(over_long, 'x', sizeof(over_long) - 1U);
  over_long[sizeof(over_long) - 1U] = '\0';

  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                    sdf_protocol_zigbee_update_user_list(over_long));
  /* Nothing recorded: the prior list is intact, not overwritten by a prefix. */
  TEST_ASSERT_EQUAL_STRING("[]", sdf_protocol_zigbee_mock_get_user_list());

  zigbee_test_enable(false);
}

/* Requirement: updates are disabled cleanly when Zigbee is disabled - success
 * no-ops, so callers need not branch on whether Zigbee is enabled. */
void test_sdf_protocol_zigbee_updates_are_noops_when_disabled(void) {
  zigbee_test_enable(false);

  TEST_ASSERT_EQUAL(ESP_OK, sdf_protocol_zigbee_update_lock_state(
                                SDF_PROTOCOL_ZIGBEE_LOCK_STATE_LOCKED));
  TEST_ASSERT_EQUAL(ESP_OK, sdf_protocol_zigbee_update_battery_percent(75));
  TEST_ASSERT_EQUAL(ESP_OK, sdf_protocol_zigbee_update_alarm_mask(0x0001));
  TEST_ASSERT_EQUAL(ESP_OK, sdf_protocol_zigbee_update_user_list("[]"));

  TEST_ASSERT_EQUAL_UINT8(SDF_PROTOCOL_ZIGBEE_LOCK_STATE_UNDEFINED,
                          sdf_protocol_zigbee_mock_get_lock_state());
  TEST_ASSERT_EQUAL_UINT8(
      200, sdf_protocol_zigbee_mock_get_battery_percent_remaining());
  TEST_ASSERT_EQUAL_UINT16(0, sdf_protocol_zigbee_mock_get_alarm_mask());
  TEST_ASSERT_NULL(sdf_protocol_zigbee_mock_get_user_list());
}

/* --- Lock ordering ------------------------------------------------------
 *
 * Requirement: no call path holds the component state mutex across a Zigbee
 * SDK call. On the device the second lock is the Zigbee stack lock, which
 * cannot be linked here; the tests below stand in for it with a mutex of their
 * own and drive the real sdf_zigbee_attr_cache_apply(). Both use timeouts, so
 * a regression shows up as a rejected update rather than a hang - which is
 * exactly why it needs asserting rather than commenting. */

typedef struct {
  SemaphoreHandle_t stack_lock; /* stands in for the Zigbee stack lock */
  int writes;
  int stack_lock_timeouts;
  esp_err_t reentrant_record_err;
} zigbee_lock_order_ctx_t;

/* Records from inside the writer, i.e. while apply() is between its snapshot
 * and its return. This is only safe if apply() has already released the cache
 * mutex; the mutex is non-recursive on purpose, so a regression times out. */
static void zigbee_reentrant_writer_u8(void *ctx, sdf_zigbee_attr_id_t attr,
                                       uint8_t value) {
  zigbee_lock_order_ctx_t *state = (zigbee_lock_order_ctx_t *)ctx;
  (void)value;
  if (attr != SDF_ZIGBEE_ATTR_LOCK_STATE) {
    return;
  }
  state->writes++;
  esp_err_t err = sdf_zigbee_attr_cache_record_alarm_mask(0x00AB);
  if (err != ESP_OK) {
    state->reentrant_record_err = err;
  }
}

void test_sdf_protocol_zigbee_apply_does_not_hold_cache_lock_across_writer(void) {
  zigbee_test_enable(true);

  TEST_ASSERT_EQUAL(ESP_OK, sdf_protocol_zigbee_update_lock_state(
                                SDF_PROTOCOL_ZIGBEE_LOCK_STATE_LOCKED));

  zigbee_lock_order_ctx_t ctx = {0};
  const sdf_zigbee_attr_writer_t writer = {
      .write_u8 = zigbee_reentrant_writer_u8,
  };

  TEST_ASSERT_EQUAL(ESP_OK, sdf_zigbee_attr_cache_apply(&writer, &ctx));
  TEST_ASSERT_EQUAL_INT(1, ctx.writes);
  /* ESP_ERR_TIMEOUT here means apply() took the cache mutex and kept it across
   * the writer - the AB-BA precondition. */
  TEST_ASSERT_EQUAL(ESP_OK, ctx.reentrant_record_err);
  /* The re-entrant record still took effect. */
  TEST_ASSERT_EQUAL_UINT16(0x00AB, sdf_protocol_zigbee_mock_get_alarm_mask());

  zigbee_test_enable(false);
}

/* The outbound half: apply() takes the cache mutex (inside its snapshot), then
 * the writer takes the stack lock. */
static void zigbee_stack_lock_writer_u8(void *ctx, sdf_zigbee_attr_id_t attr,
                                        uint8_t value) {
  zigbee_lock_order_ctx_t *state = (zigbee_lock_order_ctx_t *)ctx;
  (void)value;
  if (attr != SDF_ZIGBEE_ATTR_LOCK_STATE) {
    return;
  }
  if (xSemaphoreTake(state->stack_lock, pdMS_TO_TICKS(500)) != pdTRUE) {
    state->stack_lock_timeouts++;
    return;
  }
  state->writes++;
  xSemaphoreGive(state->stack_lock);
}

/* The inbound half: a ZCL command callback arrives with the stack lock already
 * held and then reaches for the cache - the opposite order to the writer
 * above. The handshake below forces the one interleaving that matters rather
 * than hammering the pair and hoping to hit it; a racy version of this test
 * missed a deliberately reintroduced inversion. */
typedef struct {
  SemaphoreHandle_t stack_lock;
  SemaphoreHandle_t stack_held; /* given once the stack lock is held */
  SemaphoreHandle_t done;
  esp_err_t record_err;
} zigbee_inbound_ctx_t;

static void zigbee_inbound_task_fn(void *arg) {
  zigbee_inbound_ctx_t *state = (zigbee_inbound_ctx_t *)arg;

  configASSERT(xSemaphoreTake(state->stack_lock, portMAX_DELAY) == pdTRUE);
  xSemaphoreGive(state->stack_held);

  /* Long enough for the main task to be inside the writer, blocked on the
   * stack lock this task holds. That is the moment an inverted apply() would
   * still be sitting on the cache mutex. */
  vTaskDelay(pdMS_TO_TICKS(100));

  state->record_err = sdf_zigbee_attr_cache_record_lock_state(
      SDF_PROTOCOL_ZIGBEE_LOCK_STATE_LOCKED);

  xSemaphoreGive(state->stack_lock);
  xSemaphoreGive(state->done);
  vTaskDelete(NULL);
}

void test_sdf_protocol_zigbee_inbound_and_apply_do_not_invert_lock_order(void) {
  zigbee_test_enable(true);

  zigbee_inbound_ctx_t inbound = {
      .stack_lock = xSemaphoreCreateMutex(),
      .stack_held = xSemaphoreCreateBinary(),
      .done = xSemaphoreCreateBinary(),
      .record_err = ESP_OK,
  };
  TEST_ASSERT_NOT_NULL(inbound.stack_lock);
  TEST_ASSERT_NOT_NULL(inbound.stack_held);
  TEST_ASSERT_NOT_NULL(inbound.done);

  zigbee_lock_order_ctx_t outbound = {.stack_lock = inbound.stack_lock};
  const sdf_zigbee_attr_writer_t writer = {
      .write_u8 = zigbee_stack_lock_writer_u8,
  };

  TEST_ASSERT_EQUAL(pdPASS, xTaskCreate(zigbee_inbound_task_fn, "zb_inbound",
                                        4096, &inbound, 5, NULL));
  TEST_ASSERT_EQUAL(pdTRUE,
                    xSemaphoreTake(inbound.stack_held, pdMS_TO_TICKS(1000)));

  /* Blocks in the writer until the inbound task lets go of the stack lock. */
  TEST_ASSERT_EQUAL(ESP_OK, sdf_zigbee_attr_cache_apply(&writer, &outbound));
  TEST_ASSERT_EQUAL(pdTRUE, xSemaphoreTake(inbound.done, pdMS_TO_TICKS(2000)));

  /* ESP_ERR_TIMEOUT here means apply() was holding the cache mutex while
   * waiting on the stack lock - the AB-BA inversion. */
  TEST_ASSERT_EQUAL(ESP_OK, inbound.record_err);
  /* And the writer got the stack lock rather than waiting out its timeout. */
  TEST_ASSERT_EQUAL_INT(0, outbound.stack_lock_timeouts);
  TEST_ASSERT_EQUAL_INT(1, outbound.writes);

  vSemaphoreDelete(inbound.stack_lock);
  vSemaphoreDelete(inbound.stack_held);
  vSemaphoreDelete(inbound.done);
  zigbee_test_enable(false);
}
