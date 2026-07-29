#include "sdf_state_machines.h"

#define SDF_FP_CMD_ENROLL_1 0x01u
#define SDF_FP_CMD_ENROLL_2 0x02u
#define SDF_FP_CMD_ENROLL_3 0x03u

static sdf_enrollment_result_t
sdf_enrollment_result_from_driver(sdf_fingerprint_op_result_t result) {
  switch (result) {
  case SDF_FINGERPRINT_OP_OK:
    return SDF_ENROLLMENT_RESULT_SUCCESS;
  case SDF_FINGERPRINT_OP_TIMEOUT:
    return SDF_ENROLLMENT_RESULT_TIMEOUT;
  case SDF_FINGERPRINT_OP_FULL:
    return SDF_ENROLLMENT_RESULT_FULL;
  case SDF_FINGERPRINT_OP_USER_OCCUPIED:
    return SDF_ENROLLMENT_RESULT_USER_OCCUPIED;
  case SDF_FINGERPRINT_OP_FINGER_OCCUPIED:
    return SDF_ENROLLMENT_RESULT_FINGER_OCCUPIED;
  case SDF_FINGERPRINT_OP_PROTOCOL_ERROR:
    return SDF_ENROLLMENT_RESULT_PROTOCOL_ERROR;
  case SDF_FINGERPRINT_OP_IO_ERROR:
    return SDF_ENROLLMENT_RESULT_IO_ERROR;
  case SDF_FINGERPRINT_OP_BAD_ARG:
    return SDF_ENROLLMENT_RESULT_BAD_ARG;
  case SDF_FINGERPRINT_OP_FAILED:
  case SDF_FINGERPRINT_OP_NO_MATCH:
  default:
    return SDF_ENROLLMENT_RESULT_FAILED;
  }
}

static sdf_fingerprint_enroll_step_t
sdf_enrollment_sm_cmd_to_driver_step(uint8_t cmd) {
  switch (cmd) {
  case SDF_FP_CMD_ENROLL_1:
    return SDF_FINGERPRINT_ENROLL_STEP_1;
  case SDF_FP_CMD_ENROLL_2:
    return SDF_FINGERPRINT_ENROLL_STEP_2;
  case SDF_FP_CMD_ENROLL_3:
    return SDF_FINGERPRINT_ENROLL_STEP_3;
  default:
    return SDF_FINGERPRINT_ENROLL_STEP_1;
  }
}

static uint8_t
sdf_enrollment_sm_get_retry_count(const sdf_enrollment_sm_t *sm) {
  switch (sm->state) {
  case SDF_ENROLLMENT_STATE_STEP_1:
    return sm->retry_count_step1;
  case SDF_ENROLLMENT_STATE_STEP_2:
    return sm->retry_count_step2;
  case SDF_ENROLLMENT_STATE_STEP_3:
    return sm->retry_count_step3;
  default:
    return 0;
  }
}

static uint8_t
sdf_enrollment_sm_get_max_retries(const sdf_enrollment_sm_t *sm) {
  switch (sm->state) {
  case SDF_ENROLLMENT_STATE_STEP_1:
    return sm->retry_policy.max_retries_step1;
  case SDF_ENROLLMENT_STATE_STEP_2:
    return sm->retry_policy.max_retries_step2;
  case SDF_ENROLLMENT_STATE_STEP_3:
    return sm->retry_policy.max_retries_step3;
  default:
    return 0;
  }
}

static void
sdf_enrollment_sm_increment_retry(sdf_enrollment_sm_t *sm) {
  switch (sm->state) {
  case SDF_ENROLLMENT_STATE_STEP_1:
    sm->retry_count_step1++;
    break;
  case SDF_ENROLLMENT_STATE_STEP_2:
    sm->retry_count_step2++;
    break;
  case SDF_ENROLLMENT_STATE_STEP_3:
    sm->retry_count_step3++;
    break;
  default:
    break;
  }
}

static void
sdf_enrollment_sm_reset_state(sdf_enrollment_sm_t *sm) {
  sm->state = SDF_ENROLLMENT_STATE_IDLE;
  sm->result = SDF_ENROLLMENT_RESULT_NONE;
  sm->user_id = 0;
  sm->permission = 0;
  sm->completed_steps = 0;
  sm->retry_count_step1 = 0;
  sm->retry_count_step2 = 0;
  sm->retry_count_step3 = 0;
  /* Keep retry_policy as configured */
}

void sdf_state_machines_init(void) {}

void sdf_enrollment_sm_init(sdf_enrollment_sm_t *sm) {
  if (sm == NULL) {
    return;
  }
  sdf_enrollment_sm_init_with_policy(sm, NULL);
}

void sdf_enrollment_sm_init_with_policy(sdf_enrollment_sm_t *sm,
                                        const sdf_enrollment_retry_policy_t *policy) {
  if (sm == NULL) {
    return;
  }
  sdf_enrollment_sm_reset_state(sm);
  if (policy != NULL) {
    sm->retry_policy = *policy;
  } else {
    sm->retry_policy = SDF_ENROLLMENT_DEFAULT_RETRY_POLICY;
  }
}

void sdf_enrollment_sm_reset(sdf_enrollment_sm_t *sm) {
  sdf_enrollment_sm_init(sm);
}

esp_err_t sdf_enrollment_sm_start(sdf_enrollment_sm_t *sm, uint16_t user_id,
                                  uint8_t permission) {
  if (sm == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  if (sdf_enrollment_sm_is_active(sm)) {
    sm->result = SDF_ENROLLMENT_RESULT_BUSY;
    return ESP_ERR_INVALID_STATE;
  }

  if (user_id < SDF_FINGERPRINT_USER_ID_MIN ||
      user_id > SDF_FINGERPRINT_USER_ID_MAX || permission < 1u ||
      permission > 3u) {
    sm->result = SDF_ENROLLMENT_RESULT_BAD_ARG;
    return ESP_ERR_INVALID_ARG;
  }

  sdf_enrollment_sm_init(sm);
  sm->state = SDF_ENROLLMENT_STATE_STEP_1;
  sm->user_id = user_id;
  sm->permission = permission;
  return ESP_OK;
}

bool sdf_enrollment_sm_is_active(const sdf_enrollment_sm_t *sm) {
  if (sm == NULL) {
    return false;
  }

  return sm->state == SDF_ENROLLMENT_STATE_STEP_1 ||
         sm->state == SDF_ENROLLMENT_STATE_STEP_2 ||
         sm->state == SDF_ENROLLMENT_STATE_STEP_3;
}

uint8_t sdf_enrollment_sm_current_step(const sdf_enrollment_sm_t *sm) {
  if (sm == NULL) {
    return 0;
  }

  switch (sm->state) {
  case SDF_ENROLLMENT_STATE_STEP_1:
    return 1;
  case SDF_ENROLLMENT_STATE_STEP_2:
    return 2;
  case SDF_ENROLLMENT_STATE_STEP_3:
    return 3;
  default:
    return 0;
  }
}

uint8_t sdf_enrollment_sm_current_command(const sdf_enrollment_sm_t *sm) {
  if (sm == NULL) {
    return 0;
  }

  switch (sm->state) {
  case SDF_ENROLLMENT_STATE_STEP_1:
    return SDF_FP_CMD_ENROLL_1;
  case SDF_ENROLLMENT_STATE_STEP_2:
    return SDF_FP_CMD_ENROLL_2;
  case SDF_ENROLLMENT_STATE_STEP_3:
    return SDF_FP_CMD_ENROLL_3;
  default:
    return 0;
  }
}

sdf_enrollment_state_t sdf_enrollment_sm_get_state(const sdf_enrollment_sm_t *sm) {
  if (sm == NULL) {
    return SDF_ENROLLMENT_STATE_IDLE;
  }
  return sm->state;
}

uint8_t sdf_enrollment_sm_get_completed_steps(const sdf_enrollment_sm_t *sm) {
  if (sm == NULL) {
    return 0;
  }
  return sm->completed_steps;
}

/* New enhanced API - returns next action to execute */
sdf_enroll_next_t
sdf_enrollment_sm_apply_step_result_ex(sdf_enrollment_sm_t *sm,
                                       sdf_fingerprint_op_result_t step_result) {
  sdf_enroll_next_t next = {.action = SDF_ENROLL_ACT_NONE,
                            .cmd = 0,
                            .user_id = 0,
                            .permission = 0,
                            .retry_count = 0};

  if (sm == NULL || !sdf_enrollment_sm_is_active(sm)) {
    return next;
  }

  if (step_result == SDF_FINGERPRINT_OP_OK) {
    sm->completed_steps++;

    if (sm->state == SDF_ENROLLMENT_STATE_STEP_1) {
      sm->state = SDF_ENROLLMENT_STATE_STEP_2;
      sm->result = SDF_ENROLLMENT_RESULT_NONE;
      next.action = SDF_ENROLL_ACT_EXECUTE_STEP;
      next.cmd = SDF_FINGERPRINT_ENROLL_STEP_2;
      next.user_id = sm->user_id;
      next.permission = sm->permission;
      return next;
    }

    if (sm->state == SDF_ENROLLMENT_STATE_STEP_2) {
      sm->state = SDF_ENROLLMENT_STATE_STEP_3;
      sm->result = SDF_ENROLLMENT_RESULT_NONE;
      next.action = SDF_ENROLL_ACT_EXECUTE_STEP;
      next.cmd = SDF_FINGERPRINT_ENROLL_STEP_3;
      next.user_id = sm->user_id;
      next.permission = sm->permission;
      return next;
    }

    sm->state = SDF_ENROLLMENT_STATE_SUCCESS;
    sm->result = SDF_ENROLLMENT_RESULT_SUCCESS;
    next.action = SDF_ENROLL_ACT_COMPLETE;
    next.user_id = sm->user_id;
    next.permission = sm->permission;
    return next;
  }

  /* On steps 1 and 2 (scan commands), ACK_FAIL (0x01) usually means the user
   * has not lifted their finger from the previous step or had a poor scan.
   * Retry the same step up to max_retries.
   *
   * On step 3 (store/combine command) the sensor runs immediately without a
   * finger. ACK_FAIL here means the two captured templates were incompatible
   * and cannot be merged. Retrying the store command without new scans will
   * never succeed, so we must fail the enrollment so the user can start over.
   */
  if (step_result == SDF_FINGERPRINT_OP_FAILED &&
      sm->state != SDF_ENROLLMENT_STATE_STEP_3) {
    uint8_t current_retry = sdf_enrollment_sm_get_retry_count(sm);
    uint8_t max_retries = sdf_enrollment_sm_get_max_retries(sm);

    if (current_retry < max_retries) {
      sdf_enrollment_sm_increment_retry(sm);
      next.action = SDF_ENROLL_ACT_RETRY_STEP;
      next.cmd = sdf_enrollment_sm_cmd_to_driver_step(sdf_enrollment_sm_current_command(sm));
      next.user_id = sm->user_id;
      next.permission = sm->permission;
      next.retry_count = current_retry + 1;
      return next;
    }
  }

  /* All other failures, or max retries exceeded */
  sm->state = SDF_ENROLLMENT_STATE_ERROR;
  sm->result = sdf_enrollment_result_from_driver(step_result);
  next.action = SDF_ENROLL_ACT_FAIL;
  next.user_id = sm->user_id;
  next.permission = sm->permission;
  return next;
}

/* Legacy API - kept for backward compatibility */
void sdf_enrollment_sm_apply_step_result(
    sdf_enrollment_sm_t *sm, sdf_fingerprint_op_result_t step_result) {
  (void)sdf_enrollment_sm_apply_step_result_ex(sm, step_result);
}
