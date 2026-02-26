#include "sdf_lock_flow.h"

#include <string.h>

#include "esp_log.h"
#include "sdf_protocol_ble.h"

static const char *TAG = "sdf_lock_flow";

/* ---- helpers ---- */

static void emit_progress(sdf_lock_flow_t *flow) {
  if (flow->ops.on_progress != NULL) {
    flow->ops.on_progress(flow->ops.ctx, flow->state != SDF_LOCK_FLOW_IDLE,
                          flow->requested_action, flow->retry_count);
  }
}

static int request_challenge(sdf_lock_flow_t *flow) {
  if (flow->ops.send_challenge == NULL) {
    return SDF_NUKI_RESULT_ERR_ARG;
  }
  int res = flow->ops.send_challenge(flow->ops.ctx);
  if (res == SDF_NUKI_RESULT_OK) {
    flow->state = SDF_LOCK_FLOW_WAIT_CHALLENGE;
  }
  return res;
}

static void fail(sdf_lock_flow_t *flow, const char *reason) {
  if (flow->ops.on_fail != NULL) {
    flow->ops.on_fail(flow->ops.ctx, reason);
  }
}

/* ---- public API ---- */

void sdf_lock_flow_init(sdf_lock_flow_t *flow, uint8_t max_retries,
                        const sdf_lock_flow_ops_t *ops) {
  if (flow == NULL) {
    return;
  }
  memset(flow, 0, sizeof(*flow));
  flow->state = SDF_LOCK_FLOW_IDLE;
  flow->max_retries = max_retries;
  if (ops != NULL) {
    flow->ops = *ops;
  }
}

void sdf_lock_flow_reset(sdf_lock_flow_t *flow) {
  if (flow == NULL) {
    return;
  }
  uint8_t max_retries = flow->max_retries;
  sdf_lock_flow_ops_t ops = flow->ops;
  memset(flow, 0, sizeof(*flow));
  flow->state = SDF_LOCK_FLOW_IDLE;
  flow->max_retries = max_retries;
  flow->ops = ops;
}

bool sdf_lock_flow_is_idle(const sdf_lock_flow_t *flow) {
  return flow == NULL || flow->state == SDF_LOCK_FLOW_IDLE;
}

int sdf_lock_flow_retry(sdf_lock_flow_t *flow, const char *reason) {
  if (flow->state == SDF_LOCK_FLOW_IDLE) {
    return SDF_NUKI_RESULT_OK;
  }

  if (flow->retry_count >= flow->max_retries) {
    ESP_LOGE(TAG, "Lock action 0x%02X failed after %u retries (%s)",
             flow->requested_action, (unsigned)flow->retry_count, reason);
    fail(flow, reason);
    sdf_lock_flow_reset(flow);
    emit_progress(flow);
    return SDF_NUKI_RESULT_ERR_AUTH;
  }

  flow->retry_count++;
  ESP_LOGW(TAG, "Retrying lock action 0x%02X (%u/%u): %s",
           flow->requested_action, (unsigned)flow->retry_count,
           (unsigned)flow->max_retries, reason);

  int res = request_challenge(flow);
  if (res != SDF_NUKI_RESULT_OK) {
    ESP_LOGE(TAG, "Challenge retry failed: %d", res);
    fail(flow, "challenge retry failed");
    sdf_lock_flow_reset(flow);
  }
  emit_progress(flow);
  return res;
}

int sdf_lock_flow_begin(sdf_lock_flow_t *flow, uint8_t action, uint8_t flags) {
  if (flow == NULL) {
    return SDF_NUKI_RESULT_ERR_ARG;
  }

  if (flow->state != SDF_LOCK_FLOW_IDLE) {
    return SDF_NUKI_RESULT_ERR_INCOMPLETE;
  }

  sdf_lock_flow_reset(flow);
  flow->requested_action = action;
  flow->flags = flags;

  int res = request_challenge(flow);
  if (res != SDF_NUKI_RESULT_OK) {
    sdf_lock_flow_reset(flow);
    return res;
  }

  emit_progress(flow);
  ESP_LOGI(TAG, "Started lock action flow for action=0x%02X", action);
  return SDF_NUKI_RESULT_OK;
}

void sdf_lock_flow_on_challenge(sdf_lock_flow_t *flow,
                                const sdf_nuki_message_t *msg) {
  if (flow == NULL || msg == NULL) {
    return;
  }

  if (flow->state != SDF_LOCK_FLOW_WAIT_CHALLENGE) {
    return;
  }

  int res = sdf_nuki_parse_challenge(msg, flow->nonce_nk);
  if (res != SDF_NUKI_RESULT_OK) {
    sdf_lock_flow_retry(flow, "challenge parse failed");
    return;
  }

  if (flow->ops.send_action == NULL) {
    sdf_lock_flow_retry(flow, "no send_action callback");
    return;
  }

  res = flow->ops.send_action(flow->ops.ctx, flow->requested_action,
                              flow->flags, flow->nonce_nk);
  if (res != SDF_NUKI_RESULT_OK) {
    sdf_lock_flow_retry(flow, "lock action write failed");
    return;
  }

  flow->state = SDF_LOCK_FLOW_WAIT_COMPLETION;
  emit_progress(flow);
}

void sdf_lock_flow_on_status(sdf_lock_flow_t *flow, uint8_t status) {
  if (flow == NULL || flow->state != SDF_LOCK_FLOW_WAIT_COMPLETION) {
    return;
  }

  if (status == SDF_STATUS_ACCEPTED) {
    return; /* still in progress */
  }

  if (status == SDF_STATUS_COMPLETE) {
    uint8_t action = flow->requested_action;
    sdf_lock_flow_reset(flow);
    emit_progress(flow);
    ESP_LOGI(TAG, "Lock action 0x%02X completed", action);
    if (flow->ops.on_complete != NULL) {
      flow->ops.on_complete(flow->ops.ctx, action);
    }
    return;
  }

  sdf_lock_flow_retry(flow, "unexpected status");
}

void sdf_lock_flow_on_error(sdf_lock_flow_t *flow) {
  if (flow == NULL || flow->state == SDF_LOCK_FLOW_IDLE) {
    return;
  }
  sdf_lock_flow_retry(flow, "received error report");
}
