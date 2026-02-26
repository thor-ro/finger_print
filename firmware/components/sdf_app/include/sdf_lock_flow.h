#ifndef SDF_LOCK_FLOW_H
#define SDF_LOCK_FLOW_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "sdf_protocol_ble.h"

/* ---- Lock flow FSM states ---- */

typedef enum {
  SDF_LOCK_FLOW_IDLE = 0,
  SDF_LOCK_FLOW_WAIT_CHALLENGE,
  SDF_LOCK_FLOW_WAIT_COMPLETION
} sdf_lock_flow_state_t;

/* ---- Callbacks into the application layer ---- */

typedef int (*sdf_lock_flow_send_challenge_cb)(void *ctx);
typedef int (*sdf_lock_flow_send_action_cb)(void *ctx, uint8_t action,
                                            uint8_t flags,
                                            const uint8_t *nonce_nk);
typedef void (*sdf_lock_flow_on_fail_cb)(void *ctx, const char *reason);
typedef void (*sdf_lock_flow_on_progress_cb)(void *ctx, bool in_progress,
                                             uint8_t action, uint8_t retries);
typedef void (*sdf_lock_flow_on_complete_cb)(void *ctx, uint8_t action);

typedef struct {
  sdf_lock_flow_send_challenge_cb send_challenge;
  sdf_lock_flow_send_action_cb send_action;
  sdf_lock_flow_on_fail_cb on_fail;
  sdf_lock_flow_on_progress_cb on_progress;
  sdf_lock_flow_on_complete_cb on_complete;
  void *ctx;
} sdf_lock_flow_ops_t;

/* ---- Lock flow context ---- */

typedef struct {
  sdf_lock_flow_state_t state;
  uint8_t requested_action;
  uint8_t flags;
  uint8_t retry_count;
  uint8_t max_retries;
  uint8_t nonce_nk[SDF_NUKI_CHALLENGE_NONCE_LEN];
  sdf_lock_flow_ops_t ops;
} sdf_lock_flow_t;

/* ---- Public API ---- */

/** Initialize the lock flow context. */
void sdf_lock_flow_init(sdf_lock_flow_t *flow, uint8_t max_retries,
                        const sdf_lock_flow_ops_t *ops);

/** Reset the lock flow back to idle (preserves ops and max_retries). */
void sdf_lock_flow_reset(sdf_lock_flow_t *flow);

/** Check whether the lock flow is idle. */
bool sdf_lock_flow_is_idle(const sdf_lock_flow_t *flow);

/** Begin a new lock action. Returns SDF_NUKI_RESULT_OK on success. */
int sdf_lock_flow_begin(sdf_lock_flow_t *flow, uint8_t action, uint8_t flags);

/** Call when a challenge message arrives. */
void sdf_lock_flow_on_challenge(sdf_lock_flow_t *flow,
                                const sdf_nuki_message_t *msg);

/** Call when a status message arrives during an active flow. */
void sdf_lock_flow_on_status(sdf_lock_flow_t *flow, uint8_t status);

/** Call when an error report arrives during an active flow. */
void sdf_lock_flow_on_error(sdf_lock_flow_t *flow);

/** Retry helper — called internally and may be called from app on reconnect. */
int sdf_lock_flow_retry(sdf_lock_flow_t *flow, const char *reason);

#endif /* SDF_LOCK_FLOW_H */
