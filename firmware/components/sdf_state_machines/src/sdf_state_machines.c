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

void sdf_state_machines_init(void) {}

void sdf_enrollment_sm_init(sdf_enrollment_sm_t *sm) {
  if (sm == NULL) {
    return;
  }

  sm->state = SDF_ENROLLMENT_STATE_IDLE;
  sm->result = SDF_ENROLLMENT_RESULT_NONE;
  sm->user_id = 0;
  sm->permission = 0;
  sm->completed_steps = 0;
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

  sm->state = SDF_ENROLLMENT_STATE_STEP_1;
  sm->result = SDF_ENROLLMENT_RESULT_NONE;
  sm->user_id = user_id;
  sm->permission = permission;
  sm->completed_steps = 0;
  return ESP_OK;
}

bool sdf_enrollment_sm_is_active(const sdf_enrollment_sm_t *sm) {
  if (sm == NULL) {
    return false;
  }

  return sm->state == SDF_ENROLLMENT_STATE_STEP_1 ||
         sm->state == SDF_ENROLLMENT_STATE_STEP_2 ||
         sm->state == SDF_ENROLLMENT_STATE_STEP_3 ||
         sm->state == SDF_ENROLLMENT_STATE_WAIT_ADMIN;
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

void sdf_enrollment_sm_apply_step_result(
    sdf_enrollment_sm_t *sm, sdf_fingerprint_op_result_t step_result) {
  if (sm == NULL || !sdf_enrollment_sm_is_active(sm)) {
    return;
  }

  if (step_result == SDF_FINGERPRINT_OP_OK) {
    sm->completed_steps++;

    if (sm->state == SDF_ENROLLMENT_STATE_STEP_1) {
      sm->state = SDF_ENROLLMENT_STATE_STEP_2;
      sm->result = SDF_ENROLLMENT_RESULT_NONE;
      return;
    }

    if (sm->state == SDF_ENROLLMENT_STATE_STEP_2) {
      sm->state = SDF_ENROLLMENT_STATE_STEP_3;
      sm->result = SDF_ENROLLMENT_RESULT_NONE;
      return;
    }

    sm->state = SDF_ENROLLMENT_STATE_SUCCESS;
    sm->result = SDF_ENROLLMENT_RESULT_SUCCESS;
    return;
  }

  sm->state = SDF_ENROLLMENT_STATE_ERROR;
  sm->result = sdf_enrollment_result_from_driver(step_result);
}
