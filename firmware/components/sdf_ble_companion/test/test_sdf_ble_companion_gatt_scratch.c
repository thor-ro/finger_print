#include "unity.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "sdf_ble_companion_gatt_scratch.h"

/* Single-owner GATT write staging. Every case starts from the unbound state,
 * so ordering between cases doesn't matter. See
 * openspec/changes/guard-ble-gatt-scratch-ownership. */

typedef enum {
    SCRATCH_TASK_BIND,
    SCRATCH_TASK_ACQUIRE,
    SCRATCH_TASK_RELEASE,
} scratch_task_op_t;

typedef struct {
    scratch_task_op_t op;
    TaskHandle_t handle;
    uint8_t *acquired;
    volatile bool done;
} scratch_task_ctx_t;

static void scratch_task_fn(void *arg) {
    scratch_task_ctx_t *ctx = (scratch_task_ctx_t *)arg;
    ctx->handle = xTaskGetCurrentTaskHandle();
    switch (ctx->op) {
        case SCRATCH_TASK_BIND:
            sdf_ble_companion_gatt_scratch_bind_owner();
            break;
        case SCRATCH_TASK_ACQUIRE:
            ctx->acquired = sdf_ble_companion_gatt_scratch_acquire();
            break;
        case SCRATCH_TASK_RELEASE:
            sdf_ble_companion_gatt_scratch_release();
            break;
    }
    ctx->done = true;
    vTaskDelete(NULL);
}

/* Runs one staging call on a freshly spawned task and waits for it to
 * finish, so the caller can assert on what a *different* task observed. */
static void run_on_other_task(scratch_task_ctx_t *ctx, scratch_task_op_t op) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->op = op;
    TEST_ASSERT_EQUAL(pdPASS,
                      xTaskCreate(scratch_task_fn, "scratch_test", 4096, ctx, 5, NULL));
    while (!ctx->done) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void test_gatt_scratch_acquire_release_round_trip(void) {
    sdf_ble_companion_gatt_scratch_reset_for_test();
    sdf_ble_companion_gatt_scratch_bind_owner();

    uint8_t *first = sdf_ble_companion_gatt_scratch_acquire();
    TEST_ASSERT_NOT_NULL(first);
    sdf_ble_companion_gatt_scratch_release();

    uint8_t *second = sdf_ble_companion_gatt_scratch_acquire();
    TEST_ASSERT_NOT_NULL(second);
    TEST_ASSERT_EQUAL_PTR(first, second);
    sdf_ble_companion_gatt_scratch_release();

    TEST_ASSERT_EQUAL_UINT32(0, sdf_ble_companion_gatt_scratch_violation_count());
}

void test_gatt_scratch_acquire_while_unbound_refused(void) {
    sdf_ble_companion_gatt_scratch_reset_for_test();

    TEST_ASSERT_NULL(sdf_ble_companion_gatt_scratch_acquire());
    TEST_ASSERT_EQUAL_UINT32(1, sdf_ble_companion_gatt_scratch_violation_count());
}

void test_gatt_scratch_second_acquire_refused_and_leaves_payload(void) {
    sdf_ble_companion_gatt_scratch_reset_for_test();
    sdf_ble_companion_gatt_scratch_bind_owner();

    uint8_t *held = sdf_ble_companion_gatt_scratch_acquire();
    TEST_ASSERT_NOT_NULL(held);
    memset(held, 0xA5, 16);

    TEST_ASSERT_NULL(sdf_ble_companion_gatt_scratch_acquire());
    TEST_ASSERT_EQUAL_UINT32(1, sdf_ble_companion_gatt_scratch_violation_count());

    /* The refused caller got nothing, so the in-flight payload is intact. */
    for (int i = 0; i < 16; i++) {
        TEST_ASSERT_EQUAL_HEX8(0xA5, held[i]);
    }
    sdf_ble_companion_gatt_scratch_release();
}

void test_gatt_scratch_acquire_from_non_owner_refused(void) {
    sdf_ble_companion_gatt_scratch_reset_for_test();

    /* Bind on a spawned task, then try to acquire from the runner task. */
    scratch_task_ctx_t ctx;
    run_on_other_task(&ctx, SCRATCH_TASK_BIND);

    TEST_ASSERT_NULL(sdf_ble_companion_gatt_scratch_acquire());
    TEST_ASSERT_EQUAL_UINT32(1, sdf_ble_companion_gatt_scratch_violation_count());
}

void test_gatt_scratch_release_when_unheld_is_noop(void) {
    sdf_ble_companion_gatt_scratch_reset_for_test();
    sdf_ble_companion_gatt_scratch_bind_owner();

    /* Error paths may release unconditionally, so this must not be counted. */
    sdf_ble_companion_gatt_scratch_release();
    sdf_ble_companion_gatt_scratch_release();
    TEST_ASSERT_EQUAL_UINT32(0, sdf_ble_companion_gatt_scratch_violation_count());

    TEST_ASSERT_NOT_NULL(sdf_ble_companion_gatt_scratch_acquire());
    sdf_ble_companion_gatt_scratch_release();
}

void test_gatt_scratch_release_from_non_owner_refused(void) {
    sdf_ble_companion_gatt_scratch_reset_for_test();
    sdf_ble_companion_gatt_scratch_bind_owner();

    uint8_t *held = sdf_ble_companion_gatt_scratch_acquire();
    TEST_ASSERT_NOT_NULL(held);

    scratch_task_ctx_t ctx;
    run_on_other_task(&ctx, SCRATCH_TASK_RELEASE);
    TEST_ASSERT_EQUAL_UINT32(1, sdf_ble_companion_gatt_scratch_violation_count());

    /* Still held by the owner: a non-owner release must not hand the buffer
     * to the next acquirer. */
    scratch_task_ctx_t acquire_ctx;
    run_on_other_task(&acquire_ctx, SCRATCH_TASK_ACQUIRE);
    TEST_ASSERT_NULL(acquire_ctx.acquired);

    sdf_ble_companion_gatt_scratch_release();
    TEST_ASSERT_NOT_NULL(sdf_ble_companion_gatt_scratch_acquire());
    sdf_ble_companion_gatt_scratch_release();
}

void test_gatt_scratch_second_bind_from_other_task_refused(void) {
    sdf_ble_companion_gatt_scratch_reset_for_test();
    sdf_ble_companion_gatt_scratch_bind_owner();

    scratch_task_ctx_t ctx;
    run_on_other_task(&ctx, SCRATCH_TASK_BIND);
    TEST_ASSERT_EQUAL_UINT32(1, sdf_ble_companion_gatt_scratch_violation_count());

    /* Original owner is still effective, and the usurper is not. */
    scratch_task_ctx_t acquire_ctx;
    run_on_other_task(&acquire_ctx, SCRATCH_TASK_ACQUIRE);
    TEST_ASSERT_NULL(acquire_ctx.acquired);

    TEST_ASSERT_NOT_NULL(sdf_ble_companion_gatt_scratch_acquire());
    sdf_ble_companion_gatt_scratch_release();
}

void test_gatt_scratch_rebind_same_task_is_silent(void) {
    sdf_ble_companion_gatt_scratch_reset_for_test();
    sdf_ble_companion_gatt_scratch_bind_owner();
    /* A NimBLE resync re-enters the host-sync hook on the same task. */
    sdf_ble_companion_gatt_scratch_bind_owner();

    TEST_ASSERT_EQUAL_UINT32(0, sdf_ble_companion_gatt_scratch_violation_count());
    TEST_ASSERT_NOT_NULL(sdf_ble_companion_gatt_scratch_acquire());
    sdf_ble_companion_gatt_scratch_release();
}
