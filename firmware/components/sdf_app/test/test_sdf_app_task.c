/* sdf_app task/queue behaviour (openspec change: give-sdf-app-a-task).
 *
 * Target-only. Every case here needs a real FreeRTOS scheduler with real task
 * switching plus the task watchdog, none of which the Linux host build of the
 * runner provides (sdf_app is not even compiled there), so this suite is wired
 * into firmware/test_runner only for a chip target. */

#include "unity.h"

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "sdf_event_router.h"

/* Exposed via SDF_APP_TESTING (sdf_app_test_exports.inc) */
extern esp_err_t test_sdf_app_start_task(void);
extern TaskHandle_t test_sdf_app_task_handle(void);
extern void test_sdf_app_subscribe_trampoline(sdf_event_router_type_t type);
extern void
test_sdf_app_event_trampoline(const sdf_event_router_event_t *event);
extern sdf_event_router_event_t
test_sdf_app_make_probe(sdf_event_router_type_t type,
                        sdf_event_router_priority_t priority, uint16_t tag);
extern void test_sdf_app_probe_log_reset(void);
extern uint32_t test_sdf_app_probe_handled_count(void);
extern TaskHandle_t test_sdf_app_probe_handled_task(uint32_t idx);
extern uint16_t test_sdf_app_probe_handled_tag(uint32_t idx);
extern TaskHandle_t test_sdf_app_probe_trampoline_task(void);
extern uint32_t test_sdf_app_evt_dropped(void);
extern uint32_t test_sdf_app_event_queue_depth(void);
extern void test_sdf_app_set_alarm_mask_bits(uint16_t set_bits,
                                             uint16_t clear_bits);
extern uint16_t test_sdf_app_get_alarm_mask(void);
extern void test_sdf_app_store_alarm_mask(uint16_t mask);

/* The router has no teardown seam on a chip target (its reset helper is Linux
 * only) and it can only be started once, so the whole suite shares one
 * bring-up: the app task, one router subscription for the trampoline, and the
 * router itself. */
static bool s_started;

/* The router type the trampoline is subscribed to for this suite. Any type
 * would do - the probe events never reach a handler - but AUDIT has no
 * producer inside the runner, so nothing else can land on the app queue and
 * perturb the ordering assertions. */
#define PROBE_TYPE SDF_EVENT_ROUTER_AUDIT

static void ensure_started(void) {
  if (s_started) {
    return;
  }
  TEST_ASSERT_EQUAL(ESP_OK, sdf_event_router_init());
  test_sdf_app_subscribe_trampoline(PROBE_TYPE);
  TEST_ASSERT_EQUAL(ESP_OK, test_sdf_app_start_task());
  TEST_ASSERT_NOT_NULL(test_sdf_app_task_handle());
  TEST_ASSERT_EQUAL(ESP_OK, sdf_event_router_start());
  s_started = true;
}

/* Waits for the app task to drain `expected` probes. Returns false on timeout
 * so a stuck task fails the assertion rather than hanging the runner. */
static bool wait_for_handled(uint32_t expected, uint32_t timeout_ms) {
  const int64_t deadline = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
  while (test_sdf_app_probe_handled_count() < expected) {
    if (esp_timer_get_time() > deadline) {
      return false;
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  return true;
}

/* 1.12 - an event dispatched to an app subscription is handled on the app
 * task, not on the task that ran the subscriber callback. */
void test_sdf_app_task_event_handled_on_app_task(void) {
  ensure_started();
  test_sdf_app_probe_log_reset();

  sdf_event_router_event_t event =
      test_sdf_app_make_probe(PROBE_TYPE, SDF_EVENT_ROUTER_PRIO_NORMAL, 1);
  TEST_ASSERT_EQUAL(ESP_OK, sdf_event_router_emit(
                                &event, SDF_EVENT_ROUTER_EMIT_TIMEOUT_DEFAULT_MS));

  TEST_ASSERT_TRUE_MESSAGE(wait_for_handled(1, 2000),
                           "app task never handled the emitted event");
  TEST_ASSERT_EQUAL_UINT32(1, test_sdf_app_probe_handled_count());

  TaskHandle_t app_task = test_sdf_app_task_handle();
  TaskHandle_t handled_on = test_sdf_app_probe_handled_task(0);
  TaskHandle_t dispatched_on = test_sdf_app_probe_trampoline_task();

  TEST_ASSERT_EQUAL_PTR(app_task, handled_on);
  TEST_ASSERT_NOT_NULL(dispatched_on);
  /* The subscriber callback ran on the router's dispatch task; the handling
   * happened somewhere else entirely - which is the whole point of the
   * trampoline. */
  TEST_ASSERT_TRUE(dispatched_on != app_task);
  TEST_ASSERT_TRUE(handled_on != xTaskGetCurrentTaskHandle());
}

/* 1.13 - with the app queue full, the trampoline returns without blocking and
 * the drop is counted. */
void test_sdf_app_task_full_queue_trampoline_does_not_block(void) {
  ensure_started();
  test_sdf_app_probe_log_reset();

  const uint32_t depth = test_sdf_app_event_queue_depth();
  TaskHandle_t app_task = test_sdf_app_task_handle();

  /* Suspending the only consumer is what makes "queue full" reachable at all;
   * left running, the app task drains faster than this one can fill. */
  vTaskSuspend(app_task);

  for (uint32_t i = 0; i < depth; i++) {
    sdf_event_router_event_t filler = test_sdf_app_make_probe(
        PROBE_TYPE, SDF_EVENT_ROUTER_PRIO_NORMAL, (uint16_t)(100 + i));
    test_sdf_app_event_trampoline(&filler);
  }

  const uint32_t dropped_before = test_sdf_app_evt_dropped();
  sdf_event_router_event_t overflow =
      test_sdf_app_make_probe(PROBE_TYPE, SDF_EVENT_ROUTER_PRIO_NORMAL, 999);

  const int64_t start_us = esp_timer_get_time();
  test_sdf_app_event_trampoline(&overflow);
  const int64_t elapsed_us = esp_timer_get_time() - start_us;

  TEST_ASSERT_EQUAL_UINT32(dropped_before + 1, test_sdf_app_evt_dropped());
  /* A zero-timeout send returns immediately; anything near the router's
   * 100 ms emit timeout would mean the trampoline started waiting. */
  TEST_ASSERT_TRUE_MESSAGE(elapsed_us < 10000,
                           "trampoline blocked on a full app queue");

  vTaskResume(app_task);
  TEST_ASSERT_TRUE_MESSAGE(wait_for_handled(depth, 2000),
                           "app task did not drain the filled queue");
  /* The dropped event is the one that never arrives. */
  TEST_ASSERT_EQUAL_UINT32(depth, test_sdf_app_probe_handled_count());
  for (uint32_t i = 0; i < depth; i++) {
    TEST_ASSERT_NOT_EQUAL(999, test_sdf_app_probe_handled_tag(i));
  }
}

/* 1.14 - a critical-priority event enqueued behind lower-priority ones is
 * handled first. */
void test_sdf_app_task_critical_event_handled_first(void) {
  ensure_started();
  test_sdf_app_probe_log_reset();

  const uint32_t queued = 4;
  TaskHandle_t app_task = test_sdf_app_task_handle();

  vTaskSuspend(app_task);
  for (uint32_t i = 0; i < queued; i++) {
    sdf_event_router_event_t normal = test_sdf_app_make_probe(
        PROBE_TYPE, SDF_EVENT_ROUTER_PRIO_NORMAL, (uint16_t)(10 + i));
    test_sdf_app_event_trampoline(&normal);
  }
  sdf_event_router_event_t critical =
      test_sdf_app_make_probe(PROBE_TYPE, SDF_EVENT_ROUTER_PRIO_CRITICAL, 42);
  test_sdf_app_event_trampoline(&critical);
  vTaskResume(app_task);

  TEST_ASSERT_TRUE_MESSAGE(wait_for_handled(queued + 1, 2000),
                           "app task did not drain the queued events");

  /* Enqueued last, handled first: the second hop preserves the ranking the
   * router applies on the first one. */
  TEST_ASSERT_EQUAL_UINT16(42, test_sdf_app_probe_handled_tag(0));
  /* The lower-priority events keep their arrival order behind it. */
  for (uint32_t i = 0; i < queued; i++) {
    TEST_ASSERT_EQUAL_UINT16((uint16_t)(10 + i),
                             test_sdf_app_probe_handled_tag(i + 1));
  }
}

/* 1.15 - the app task is watchdog-registered while running. */
void test_sdf_app_task_is_watchdog_registered(void) {
  ensure_started();

  TaskHandle_t app_task = test_sdf_app_task_handle();
  TEST_ASSERT_NOT_NULL(app_task);

  /* sdf_platform_time_wdt_add() is the task's first action, but it runs
   * asynchronously with respect to this task; give it a moment to get there. */
  const int64_t deadline = esp_timer_get_time() + 2000 * 1000;
  esp_err_t status = esp_task_wdt_status(app_task);
  while (status != ESP_OK && esp_timer_get_time() < deadline) {
    vTaskDelay(pdMS_TO_TICKS(5));
    status = esp_task_wdt_status(app_task);
  }

  TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, status,
                            "sdf_app task is not subscribed to the TWDT");
}

/* --- 3.5: alarm-mask read-modify-write ----------------------------------- */

#define MASK_LOW 0x00FFu  /* owned by the setter task */
#define MASK_HIGH 0xFF00u /* owned by the clearer task */
#define MASK_STRESS_MS 1000

/* Deterministic half of 3.5: the composition rule itself, with no scheduling
 * involved. Runs on this task alone, so it proves what the set/clear pair is
 * supposed to compute - the concurrent case below can then be about lost
 * updates rather than about arithmetic. */
void test_sdf_app_alarm_mask_composition_single_threaded(void) {
  test_sdf_app_store_alarm_mask(0);

  test_sdf_app_set_alarm_mask_bits(0x0001, 0);
  TEST_ASSERT_EQUAL_HEX16(0x0001, test_sdf_app_get_alarm_mask());

  /* Setting is additive: an unrelated bit is left alone. */
  test_sdf_app_set_alarm_mask_bits(0x0100, 0);
  TEST_ASSERT_EQUAL_HEX16(0x0101, test_sdf_app_get_alarm_mask());

  /* A set and a clear of *different* bits in one call take both effects. */
  test_sdf_app_set_alarm_mask_bits(0x0010, 0x0100);
  TEST_ASSERT_EQUAL_HEX16(0x0011, test_sdf_app_get_alarm_mask());

  /* Clearing a bit that is already clear is a no-op, not a toggle. */
  test_sdf_app_set_alarm_mask_bits(0, 0x0100);
  TEST_ASSERT_EQUAL_HEX16(0x0011, test_sdf_app_get_alarm_mask());

  /* Overlapping set and clear: the clear is applied last, so it wins. */
  test_sdf_app_set_alarm_mask_bits(0x0020, 0x0020);
  TEST_ASSERT_EQUAL_HEX16(0x0011, test_sdf_app_get_alarm_mask());

  /* Redundant update (no bit actually changes) leaves the mask untouched. */
  test_sdf_app_set_alarm_mask_bits(0x0011, 0);
  TEST_ASSERT_EQUAL_HEX16(0x0011, test_sdf_app_get_alarm_mask());

  test_sdf_app_set_alarm_mask_bits(0, 0xFFFF);
  TEST_ASSERT_EQUAL_HEX16(0x0000, test_sdf_app_get_alarm_mask());
}

typedef struct {
  uint16_t bits;             /* the half of the mask this writer owns */
  volatile uint32_t rounds;  /* set/clear pairs completed */
  volatile uint32_t lost;    /* times another writer clobbered our bits */
  volatile bool done;
} mask_writer_t;

/* Each writer only ever touches its own half of the mask, so its own half must
 * read back exactly what it just wrote. Anything else means the other writer's
 * read-modify-write swallowed this one - the failure a plain load/store
 * implementation produces and the compare-exchange loop prevents. */
static void mask_writer_task(void *arg) {
  mask_writer_t *w = (mask_writer_t *)arg;
  const int64_t deadline = esp_timer_get_time() + (int64_t)MASK_STRESS_MS * 1000;

  while (esp_timer_get_time() < deadline) {
    test_sdf_app_set_alarm_mask_bits(w->bits, 0);
    if ((test_sdf_app_get_alarm_mask() & w->bits) != w->bits) {
      w->lost++;
    }
    test_sdf_app_set_alarm_mask_bits(0, w->bits);
    if ((test_sdf_app_get_alarm_mask() & w->bits) != 0) {
      w->lost++;
    }
    w->rounds++;
  }

  w->done = true;
  vTaskDelete(NULL);
}

/* 3.5 - a set of A and a clear of B racing on the same mask converge to both
 * effects.
 *
 * Note what this can and cannot claim on a single-core chip: the two writers
 * run at the same priority with time slicing, so preemption lands inside the
 * load/compare-exchange window many times over the run, but nothing here
 * *forces* it to land there on any given iteration. It is a stress test, not a
 * proof - which is why the composition rule itself is pinned down
 * deterministically by the single-threaded case above. */
void test_sdf_app_alarm_mask_concurrent_set_and_clear_converge(void) {
  static mask_writer_t setter;
  static mask_writer_t clearer;

  setter = (mask_writer_t){.bits = MASK_LOW};
  clearer = (mask_writer_t){.bits = MASK_HIGH};

  test_sdf_app_store_alarm_mask(MASK_HIGH);

  /* Equal priorities, above this task, so the scheduler round-robins the two
   * writers against each other instead of running one to completion. */
  const UBaseType_t writer_prio = uxTaskPriorityGet(NULL) + 1;
  TEST_ASSERT_EQUAL(pdPASS, xTaskCreate(mask_writer_task, "mask_lo", 2560,
                                        &setter, writer_prio, NULL));
  TEST_ASSERT_EQUAL(pdPASS, xTaskCreate(mask_writer_task, "mask_hi", 2560,
                                        &clearer, writer_prio, NULL));

  const int64_t deadline =
      esp_timer_get_time() + (int64_t)(MASK_STRESS_MS + 2000) * 1000;
  while (!setter.done || !clearer.done) {
    TEST_ASSERT_TRUE_MESSAGE(esp_timer_get_time() < deadline,
                             "alarm mask writers did not finish");
    vTaskDelay(pdMS_TO_TICKS(10));
  }

  /* Both writers really ran; a near-zero count would make the assertions
   * below vacuous. */
  TEST_ASSERT_GREATER_THAN_UINT32(1000, setter.rounds);
  TEST_ASSERT_GREATER_THAN_UINT32(1000, clearer.rounds);

  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, setter.lost,
                                   "low-half update lost to a concurrent RMW");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, clearer.lost,
                                   "high-half update lost to a concurrent RMW");

  /* Both writers ended on a clear of their own half, and neither ever touched
   * the other's: the two effects composed. */
  TEST_ASSERT_EQUAL_HEX16(0x0000, test_sdf_app_get_alarm_mask());

  test_sdf_app_store_alarm_mask(0);
}
