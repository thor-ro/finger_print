#include "sdf_lock_flow.h"
#include "unity.h"

/* --------------- Init / Reset --------------- */

void test_lock_flow_init(void) {
  sdf_lock_flow_t flow;
  sdf_lock_flow_init(&flow, 3);
  TEST_ASSERT_EQUAL(SDF_LOCK_FLOW_IDLE, flow.state);
  TEST_ASSERT_EQUAL(0, flow.requested_action);
  TEST_ASSERT_EQUAL(0, flow.flags);
  TEST_ASSERT_EQUAL(0, flow.retry_count);
  TEST_ASSERT_EQUAL(3, flow.max_retries);
}

void test_lock_flow_reset_preserves_max_retries(void) {
  sdf_lock_flow_t flow;
  sdf_lock_flow_init(&flow, 5);
  flow.state = SDF_LOCK_FLOW_WAIT_CHALLENGE;
  flow.requested_action = 0x01;
  flow.retry_count = 2;

  sdf_lock_flow_reset(&flow);
  TEST_ASSERT_EQUAL(SDF_LOCK_FLOW_IDLE, flow.state);
  TEST_ASSERT_EQUAL(0, flow.requested_action);
  TEST_ASSERT_EQUAL(0, flow.retry_count);
  TEST_ASSERT_EQUAL(5, flow.max_retries);
}

/* --------------- is_idle --------------- */

void test_lock_flow_is_idle_on_init(void) {
  sdf_lock_flow_t flow;
  sdf_lock_flow_init(&flow, 2);
  TEST_ASSERT_TRUE(sdf_lock_flow_is_idle(&flow));
}

void test_lock_flow_is_not_idle_when_active(void) {
  sdf_lock_flow_t flow;
  sdf_lock_flow_init(&flow, 2);
  sdf_lock_flow_start(&flow, 0x01, 0);
  sdf_lock_flow_set_wait_challenge(&flow);
  TEST_ASSERT_FALSE(sdf_lock_flow_is_idle(&flow));
}

/* --------------- start --------------- */

void test_lock_flow_start_success(void) {
  sdf_lock_flow_t flow;
  sdf_lock_flow_init(&flow, 2);
  TEST_ASSERT_TRUE(sdf_lock_flow_start(&flow, 0x01, 0x02));
  TEST_ASSERT_EQUAL(0x01, flow.requested_action);
  TEST_ASSERT_EQUAL(0x02, flow.flags);
  TEST_ASSERT_EQUAL(SDF_LOCK_FLOW_IDLE, flow.state);
}

void test_lock_flow_start_rejected_when_active(void) {
  sdf_lock_flow_t flow;
  sdf_lock_flow_init(&flow, 2);
  sdf_lock_flow_start(&flow, 0x01, 0);
  sdf_lock_flow_set_wait_challenge(&flow);

  TEST_ASSERT_FALSE(sdf_lock_flow_start(&flow, 0x03, 0));
  TEST_ASSERT_EQUAL(0x01, flow.requested_action);
}

/* --------------- state transitions --------------- */

void test_lock_flow_state_transitions(void) {
  sdf_lock_flow_t flow;
  sdf_lock_flow_init(&flow, 2);
  sdf_lock_flow_start(&flow, 0x01, 0);

  sdf_lock_flow_set_wait_challenge(&flow);
  TEST_ASSERT_EQUAL(SDF_LOCK_FLOW_WAIT_CHALLENGE, flow.state);

  sdf_lock_flow_set_wait_completion(&flow);
  TEST_ASSERT_EQUAL(SDF_LOCK_FLOW_WAIT_COMPLETION, flow.state);
}

/* --------------- retries --------------- */

void test_lock_flow_retries_not_exhausted(void) {
  sdf_lock_flow_t flow;
  sdf_lock_flow_init(&flow, 2);
  TEST_ASSERT_FALSE(sdf_lock_flow_retries_exhausted(&flow));
}

void test_lock_flow_advance_retry(void) {
  sdf_lock_flow_t flow;
  sdf_lock_flow_init(&flow, 2);
  TEST_ASSERT_TRUE(sdf_lock_flow_advance_retry(&flow));
  TEST_ASSERT_EQUAL(1, flow.retry_count);
  TEST_ASSERT_TRUE(sdf_lock_flow_advance_retry(&flow));
  TEST_ASSERT_EQUAL(2, flow.retry_count);
  TEST_ASSERT_FALSE(sdf_lock_flow_advance_retry(&flow));
  TEST_ASSERT_EQUAL(2, flow.retry_count);
}

void test_lock_flow_retries_exhausted_at_max(void) {
  sdf_lock_flow_t flow;
  sdf_lock_flow_init(&flow, 1);
  sdf_lock_flow_advance_retry(&flow);
  TEST_ASSERT_TRUE(sdf_lock_flow_retries_exhausted(&flow));
}

void test_lock_flow_zero_max_retries(void) {
  sdf_lock_flow_t flow;
  sdf_lock_flow_init(&flow, 0);
  TEST_ASSERT_TRUE(sdf_lock_flow_retries_exhausted(&flow));
  TEST_ASSERT_FALSE(sdf_lock_flow_advance_retry(&flow));
}
