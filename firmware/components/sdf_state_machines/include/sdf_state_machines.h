#ifndef SDF_STATE_MACHINES_H
#define SDF_STATE_MACHINES_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#include "fingerprint.h"

typedef enum {
  SDF_ENROLLMENT_STATE_IDLE = 0,
  SDF_ENROLLMENT_STATE_STEP_1 = 1,
  SDF_ENROLLMENT_STATE_STEP_2 = 2,
  SDF_ENROLLMENT_STATE_STEP_3 = 3,
  SDF_ENROLLMENT_STATE_SUCCESS = 4,
  SDF_ENROLLMENT_STATE_ERROR = 5,
} sdf_enrollment_state_t;

typedef enum {
  SDF_ENROLLMENT_RESULT_NONE = 0,
  SDF_ENROLLMENT_RESULT_SUCCESS = 1,
  SDF_ENROLLMENT_RESULT_FAILED = 2,
  SDF_ENROLLMENT_RESULT_TIMEOUT = 3,
  SDF_ENROLLMENT_RESULT_FULL = 4,
  SDF_ENROLLMENT_RESULT_USER_OCCUPIED = 5,
  SDF_ENROLLMENT_RESULT_FINGER_OCCUPIED = 6,
  SDF_ENROLLMENT_RESULT_PROTOCOL_ERROR = 7,
  SDF_ENROLLMENT_RESULT_IO_ERROR = 8,
  SDF_ENROLLMENT_RESULT_BAD_ARG = 9,
  SDF_ENROLLMENT_RESULT_BUSY = 10,
} sdf_enrollment_result_t;

/* ---- Enhanced Enrollment Action API ---- */

typedef enum {
  SDF_ENROLL_ACT_NONE = 0,
  SDF_ENROLL_ACT_EXECUTE_STEP = 1,
  SDF_ENROLL_ACT_RETRY_STEP = 2,
  SDF_ENROLL_ACT_COMPLETE = 3,
  SDF_ENROLL_ACT_FAIL = 4,
} sdf_enroll_action_t;

typedef struct {
  uint8_t max_retries_step1;
  uint8_t max_retries_step2;
  uint8_t max_retries_step3;
} sdf_enrollment_retry_policy_t;

typedef struct {
  sdf_enroll_action_t action;
  sdf_fingerprint_enroll_step_t cmd;
  uint16_t user_id;
  uint8_t permission;
  uint8_t retry_count;
} sdf_enroll_next_t;

/* Default retry policy. Steps 1 and 2 each wait for a fresh finger press, so
 * their retries are the user's second and third chances to lift and place -
 * at SDF_ENROLL_RETRY_INTERVAL_MS apart that is the window they actually
 * get. Step 3 is never retried and its budget here is inert: the state
 * machine short-circuits it deliberately, because a store that reports the
 * captures could not be merged will report the same on a retry without new
 * scans. Kept at zero so the value does not imply otherwise. */
#define SDF_ENROLLMENT_DEFAULT_RETRY_POLICY \
  (sdf_enrollment_retry_policy_t){ .max_retries_step1 = 5, .max_retries_step2 = 5, .max_retries_step3 = 0 }

typedef struct {
  sdf_enrollment_state_t state;
  sdf_enrollment_result_t result;
  uint16_t user_id;
  uint8_t permission;
  uint8_t completed_steps;
  uint8_t retry_count_step1;
  uint8_t retry_count_step2;
  uint8_t retry_count_step3;
  sdf_enrollment_retry_policy_t retry_policy;
} sdf_enrollment_sm_t;

void sdf_state_machines_init(void);

void sdf_enrollment_sm_init(sdf_enrollment_sm_t *sm);
void sdf_enrollment_sm_reset(sdf_enrollment_sm_t *sm);

/* Initialize with custom retry policy (pass NULL for defaults) */
void sdf_enrollment_sm_init_with_policy(sdf_enrollment_sm_t *sm,
                                        const sdf_enrollment_retry_policy_t *policy);

esp_err_t sdf_enrollment_sm_start(sdf_enrollment_sm_t *sm, uint16_t user_id,
                                  uint8_t permission);
bool sdf_enrollment_sm_is_active(const sdf_enrollment_sm_t *sm);
uint8_t sdf_enrollment_sm_current_command(const sdf_enrollment_sm_t *sm);
uint8_t sdf_enrollment_sm_current_step(const sdf_enrollment_sm_t *sm);
sdf_enrollment_state_t sdf_enrollment_sm_get_state(const sdf_enrollment_sm_t *sm);
uint8_t sdf_enrollment_sm_get_completed_steps(const sdf_enrollment_sm_t *sm);

/* New enhanced API - returns next action to execute */
sdf_enroll_next_t sdf_enrollment_sm_apply_step_result_ex(
    sdf_enrollment_sm_t *sm, sdf_fingerprint_op_result_t step_result);

/* Legacy API - kept for backward compatibility */
void sdf_enrollment_sm_apply_step_result(
    sdf_enrollment_sm_t *sm, sdf_fingerprint_op_result_t step_result);

#endif /* SDF_STATE_MACHINES_H */
