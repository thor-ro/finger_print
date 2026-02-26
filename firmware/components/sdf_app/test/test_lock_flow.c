#include "sdf_lock_flow.h"
#include "unity.h"

/* Stub callbacks for testing */
static int s_challenge_calls;
static int s_action_calls;
static int s_fail_calls;
static int s_progress_calls;
static int s_complete_calls;
static uint8_t s_completed_action;
static bool s_last_in_progress;

static int stub_send_challenge(void *ctx) {
  (void)ctx;
  s_challenge_calls++;
  return SDF_NUKI_RESULT_OK;
}

static int stub_send_action(void *ctx, uint8_t action, uint8_t flags,
                            const uint8_t *nonce_nk) {
  (void)ctx;
  (void)action;
  (void)flags;
  (void)nonce_nk;
  s_action_calls++;
  return SDF_NUKI_RESULT_OK;
}

static void stub_on_fail(void *ctx, const char *reason) {
  (void)ctx;
  (void)reason;
  s_fail_calls++;
}

static void stub_on_progress(void *ctx, bool in_progress, uint8_t action,
                             uint8_t retries) {
  (void)ctx;
  (void)action;
  (void)retries;
  s_progress_calls++;
  s_last_in_progress = in_progress;
}

static void stub_on_complete(void *ctx, uint8_t action) {
  (void)ctx;
  s_complete_calls++;
  s_completed_action = action;
}

static const sdf_lock_flow_ops_t s_ops = {
    .send_challenge = stub_send_challenge,
    .send_action = stub_send_action,
    .on_fail = stub_on_fail,
    .on_progress = stub_on_progress,
    .on_complete = stub_on_complete,
    .ctx = NULL,
};

static void reset_counters(void) {
  s_challenge_calls = 0;
  s_action_calls = 0;
  s_fail_calls = 0;
  s_progress_calls = 0;
  s_complete_calls = 0;
  s_completed_action = 0;
  s_last_in_progress = false;
}

/* --------------- Init / Reset --------------- */

void test_lock_flow_init(void) {
  sdf_lock_flow_t flow;
  sdf_lock_flow_init(&flow, 3, &s_ops);
  TEST_ASSERT_EQUAL(SDF_LOCK_FLOW_IDLE, flow.state);
  TEST_ASSERT_EQUAL(0, flow.requested_action);
  TEST_ASSERT_EQUAL(0, flow.flags);
  TEST_ASSERT_EQUAL(0, flow.retry_count);
  TEST_ASSERT_EQUAL(3, flow.max_retries);
}

void test_lock_flow_reset_preserves_max_retries(void) {
  sdf_lock_flow_t flow;
  sdf_lock_flow_init(&flow, 5, &s_ops);
  flow.state = SDF_LOCK_FLOW_WAIT_CHALLENGE;
  flow.requested_action = 0x01;
  flow.retry_count = 2;

  sdf_lock_flow_reset(&flow);
  TEST_ASSERT_EQUAL(SDF_LOCK_FLOW_IDLE, flow.state);
  TEST_ASSERT_EQUAL(0, flow.requested_action);
  TEST_ASSERT_EQUAL(0, flow.retry_count);
  TEST_ASSERT_EQUAL(5, flow.max_retries);
  /* ops should also be preserved */
  TEST_ASSERT_NOT_NULL(flow.ops.send_challenge);
}

/* --------------- is_idle --------------- */

void test_lock_flow_is_idle_on_init(void) {
  sdf_lock_flow_t flow;
  sdf_lock_flow_init(&flow, 2, &s_ops);
  TEST_ASSERT_TRUE(sdf_lock_flow_is_idle(&flow));
}

void test_lock_flow_is_not_idle_when_active(void) {
  sdf_lock_flow_t flow;
  reset_counters();
  sdf_lock_flow_init(&flow, 2, &s_ops);
  sdf_lock_flow_begin(&flow, 0x01, 0);
  TEST_ASSERT_FALSE(sdf_lock_flow_is_idle(&flow));
}

/* --------------- begin --------------- */

void test_lock_flow_begin_success(void) {
  sdf_lock_flow_t flow;
  reset_counters();
  sdf_lock_flow_init(&flow, 2, &s_ops);
  int res = sdf_lock_flow_begin(&flow, 0x01, 0x02);
  TEST_ASSERT_EQUAL(SDF_NUKI_RESULT_OK, res);
  TEST_ASSERT_EQUAL(0x01, flow.requested_action);
  TEST_ASSERT_EQUAL(0x02, flow.flags);
  TEST_ASSERT_EQUAL(SDF_LOCK_FLOW_WAIT_CHALLENGE, flow.state);
  TEST_ASSERT_EQUAL(1, s_challenge_calls);
  TEST_ASSERT_EQUAL(1, s_progress_calls);
}

void test_lock_flow_begin_rejected_when_active(void) {
  sdf_lock_flow_t flow;
  reset_counters();
  sdf_lock_flow_init(&flow, 2, &s_ops);
  sdf_lock_flow_begin(&flow, 0x01, 0);

  int res = sdf_lock_flow_begin(&flow, 0x03, 0);
  TEST_ASSERT_EQUAL(SDF_NUKI_RESULT_ERR_INCOMPLETE, res);
  TEST_ASSERT_EQUAL(0x01, flow.requested_action);
}

/* --------------- retry --------------- */

void test_lock_flow_retry_increments(void) {
  sdf_lock_flow_t flow;
  reset_counters();
  sdf_lock_flow_init(&flow, 3, &s_ops);
  sdf_lock_flow_begin(&flow, 0x01, 0);

  int res = sdf_lock_flow_retry(&flow, "test");
  TEST_ASSERT_EQUAL(SDF_NUKI_RESULT_OK, res);
  TEST_ASSERT_EQUAL(1, flow.retry_count);
  TEST_ASSERT_EQUAL(SDF_LOCK_FLOW_WAIT_CHALLENGE, flow.state);
}

void test_lock_flow_retry_exhaustion(void) {
  sdf_lock_flow_t flow;
  reset_counters();
  sdf_lock_flow_init(&flow, 1, &s_ops);
  sdf_lock_flow_begin(&flow, 0x01, 0);
  sdf_lock_flow_retry(&flow, "try1");

  /* retry_count is now 1 == max_retries, next retry should fail */
  int res = sdf_lock_flow_retry(&flow, "try2");
  TEST_ASSERT_EQUAL(SDF_NUKI_RESULT_ERR_AUTH, res);
  TEST_ASSERT_TRUE(sdf_lock_flow_is_idle(&flow));
  TEST_ASSERT_EQUAL(1, s_fail_calls);
}

/* --------------- on_status --------------- */

void test_lock_flow_on_status_complete(void) {
  sdf_lock_flow_t flow;
  reset_counters();
  sdf_lock_flow_init(&flow, 2, &s_ops);
  sdf_lock_flow_begin(&flow, 0x01, 0);
  /* move to WAIT_COMPLETION */
  flow.state = SDF_LOCK_FLOW_WAIT_COMPLETION;

  sdf_lock_flow_on_status(&flow, SDF_STATUS_COMPLETE);
  TEST_ASSERT_TRUE(sdf_lock_flow_is_idle(&flow));
  TEST_ASSERT_EQUAL(1, s_complete_calls);
  TEST_ASSERT_EQUAL(0x01, s_completed_action);
}

void test_lock_flow_on_status_accepted_noop(void) {
  sdf_lock_flow_t flow;
  reset_counters();
  sdf_lock_flow_init(&flow, 2, &s_ops);
  sdf_lock_flow_begin(&flow, 0x01, 0);
  flow.state = SDF_LOCK_FLOW_WAIT_COMPLETION;

  sdf_lock_flow_on_status(&flow, SDF_STATUS_ACCEPTED);
  TEST_ASSERT_FALSE(sdf_lock_flow_is_idle(&flow));
  TEST_ASSERT_EQUAL(0, s_complete_calls);
}
