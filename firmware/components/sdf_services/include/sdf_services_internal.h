#ifndef SDF_SERVICES_INTERNAL_H
#define SDF_SERVICES_INTERNAL_H

#include <stddef.h>

#include "sdf_services.h"
#include "sdf_lock_guard.h"
#include "sdf_storage.h"

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
  TaskHandle_t task;              /* Legacy task handle for backward compat */
  TaskHandle_t match_task;
  TaskHandle_t enroll_task;
  TaskHandle_t admin_task;
  TaskHandle_t button_task;
  /* Set by sdf_services_stop_tasks() and polled (under s_state.lock) by
   * each task's own main loop so tasks exit and clean up (unsubscribe from
   * the event router, self-delete) cooperatively instead of being killed
   * from outside via vTaskDelete() - which could leave s_state.lock
   * permanently held if the victim task happened to be inside a critical
   * section at the moment of deletion. */
  bool stop_requested;
  bool initialized;
  sdf_services_config_t config;
  sdf_enrollment_sm_t enrollment;
  QueueHandle_t match_task_queue;
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

  /* Web Companion registration authorization */
  bool web_reg_auth_pending;
  char request_web_username[SDF_STORAGE_WEB_USER_NAME_MAX];
  uint8_t request_web_password_hash[SDF_STORAGE_WEB_USER_HASH_LEN];
  uint8_t request_web_permission;
  int64_t web_reg_auth_start_us;

#ifndef CONFIG_IDF_TARGET_LINUX
  button_handle_t btn_handle;
#endif
} sdf_services_state_t;

sdf_services_state_t *sdf_services_state(void);
const char *sdf_services_fingerprint_result_name(
    sdf_fingerprint_op_result_t result);
void sdf_services_run_enrollment_step(void);
void sdf_services_start_pending_enrollment_if_any(void);

/* Shared internal functions (moved from static) */
esp_err_t sdf_services_fingerprint_result_to_err(sdf_fingerprint_op_result_t result);
bool sdf_services_try_claim_admin_action(const sdf_fingerprint_match_t *match);
void sdf_services_complete_permission_change(esp_err_t result);
void sdf_services_execute_admin_action(sdf_services_admin_action_t action,
                                       sdf_services_admin_action_cb action_cb,
                                       void *action_ctx);

/* New task declarations */
void sdf_match_task(void *arg);
void sdf_enroll_task(void *arg);
void sdf_admin_task(void *arg);
void sdf_button_task(void *arg);

/* Task start/stop */
esp_err_t sdf_services_start_tasks(void);
esp_err_t sdf_services_stop_tasks(void);

/* Bitmap helpers */
#define SDF_SERVICES_BMP_TEST(bmp, id)  ((bmp) & (1u << ((id) - 1)))
#define SDF_SERVICES_BMP_SET(bmp, id)   ((bmp) |= (1u << ((id) - 1)))
#define SDF_SERVICES_BMP_CLEAR(bmp, id) ((bmp) &= ~(1u << ((id) - 1)))

static inline uint8_t sdf_services_perm_get(const uint8_t *packed, uint16_t id)
{
    uint8_t byte = packed[(id - 1) / 4];
    uint8_t shift = ((id - 1) % 4) * 2;
    return (byte >> shift) & 0x3;
}

static inline void sdf_services_perm_set(uint8_t *packed, uint16_t id, uint8_t perm)
{
    uint8_t *byte = &packed[(id - 1) / 4];
    uint8_t shift = ((id - 1) % 4) * 2;
    *byte = (*byte & ~(0x3 << shift)) | ((perm & 0x3) << shift);
}

/* Pack sensor query results into compact bitmap + packed permissions */
void sdf_services_pack_user_list(const uint16_t *user_ids,
                                        const uint8_t *permissions,
                                        size_t count,
                                        uint16_t *bmp,
                                        uint8_t *perm_packed);

#endif /* SDF_SERVICES_INTERNAL_H */
