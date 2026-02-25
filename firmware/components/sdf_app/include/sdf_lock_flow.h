#ifndef SDF_LOCK_FLOW_H
#define SDF_LOCK_FLOW_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "sdf_protocol_ble.h"

typedef enum {
  SDF_LOCK_FLOW_IDLE = 0,
  SDF_LOCK_FLOW_WAIT_CHALLENGE,
  SDF_LOCK_FLOW_WAIT_COMPLETION
} sdf_lock_flow_state_t;

typedef struct {
  sdf_lock_flow_state_t state;
  uint8_t requested_action;
  uint8_t flags;
  uint8_t retry_count;
  uint8_t max_retries;
  uint8_t nonce_nk[SDF_NUKI_CHALLENGE_NONCE_LEN];
} sdf_lock_flow_t;

/**
 * @brief Initialize a lock flow context to idle.
 */
static inline void sdf_lock_flow_init(sdf_lock_flow_t *flow,
                                      uint8_t max_retries) {
  memset(flow, 0, sizeof(*flow));
  flow->state = SDF_LOCK_FLOW_IDLE;
  flow->max_retries = max_retries;
}

/**
 * @brief Reset a lock flow context back to idle.
 */
static inline void sdf_lock_flow_reset(sdf_lock_flow_t *flow) {
  uint8_t max_retries = flow->max_retries;
  memset(flow, 0, sizeof(*flow));
  flow->state = SDF_LOCK_FLOW_IDLE;
  flow->max_retries = max_retries;
}

/**
 * @brief Check if the lock flow is idle (no action in progress).
 */
static inline bool sdf_lock_flow_is_idle(const sdf_lock_flow_t *flow) {
  return flow->state == SDF_LOCK_FLOW_IDLE;
}

/**
 * @brief Check if the retry limit has been reached.
 */
static inline bool
sdf_lock_flow_retries_exhausted(const sdf_lock_flow_t *flow) {
  return flow->retry_count >= flow->max_retries;
}

/**
 * @brief Prepare a new lock action. Returns false if flow is already active.
 */
static inline bool sdf_lock_flow_start(sdf_lock_flow_t *flow,
                                       uint8_t lock_action, uint8_t flags) {
  if (flow->state != SDF_LOCK_FLOW_IDLE) {
    return false;
  }
  sdf_lock_flow_reset(flow);
  flow->requested_action = lock_action;
  flow->flags = flags;
  return true;
}

/**
 * @brief Mark the flow as waiting for a challenge response.
 */
static inline void sdf_lock_flow_set_wait_challenge(sdf_lock_flow_t *flow) {
  flow->state = SDF_LOCK_FLOW_WAIT_CHALLENGE;
}

/**
 * @brief Mark the flow as waiting for completion after lock action was sent.
 */
static inline void sdf_lock_flow_set_wait_completion(sdf_lock_flow_t *flow) {
  flow->state = SDF_LOCK_FLOW_WAIT_COMPLETION;
}

/**
 * @brief Increment the retry counter. Returns false if retries are exhausted.
 */
static inline bool sdf_lock_flow_advance_retry(sdf_lock_flow_t *flow) {
  if (flow->retry_count >= flow->max_retries) {
    return false;
  }
  flow->retry_count++;
  return true;
}

#endif /* SDF_LOCK_FLOW_H */
