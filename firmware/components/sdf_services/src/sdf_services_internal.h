#ifndef SDF_SERVICES_INTERNAL_H
#define SDF_SERVICES_INTERNAL_H

#include <stddef.h>

#include "sdf_services.h"
#include "sdf_lock_guard.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#ifndef CONFIG_IDF_TARGET_LINUX
#include "iot_button.h"
#endif

#define SDF_SERVICES_LOCK_WAIT_MS 250u

typedef struct {
  SemaphoreHandle_t lock;
  SemaphoreHandle_t wake_sem;
  SemaphoreHandle_t admin_action_done_sem;
  TaskHandle_t task;
  bool initialized;
  sdf_services_config_t config;
  sdf_enrollment_sm_t enrollment;
  bool enrollment_request_pending;
  uint16_t request_user_id;
  uint8_t request_permission;
  int64_t match_cooldown_until_us;
  uint32_t failed_attempt_count;
  int64_t failed_attempt_window_start_us;
  int64_t lockout_until_us;
  sdf_services_admin_action_t pending_admin_action;
  int64_t pending_admin_action_start_us;
  bool permission_change_pending;
  uint16_t permission_change_user_id;
  uint8_t permission_change_permission;
  esp_err_t permission_change_result;
  size_t enrolled_user_count;
#ifndef CONFIG_IDF_TARGET_LINUX
  button_handle_t btn_handle;
#endif
} sdf_services_state_t;

sdf_services_state_t *sdf_services_state(void);
const char *sdf_services_fingerprint_result_name(
    sdf_fingerprint_op_result_t result);
void sdf_services_run_enrollment_step(void);
void sdf_services_start_pending_enrollment_if_any(void);

#endif /* SDF_SERVICES_INTERNAL_H */
