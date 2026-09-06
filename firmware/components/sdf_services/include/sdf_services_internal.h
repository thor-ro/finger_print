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
  /* Set by sdf_services_stop_tasks() and polled (under s_state.lock) by
   * each task's own main loop so tasks exit and clean up (clearing their
   * queues and self-deleting) cooperatively instead of being killed
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
  /* Believed state of the persisted NVS lockout record
   * (persist-biometric-lockout): true from lockout entry or boot restore
   * until a "cleared" write lands. The successful-match path consults it so
   * it can heal a stale armed record with a single write without putting an
   * NVS write on every unlock - matching is impossible while a live lockout
   * is armed, so this flag is only ever true there if the record outlived
   * its RAM deadline (e.g. the armed episode expired before the first scan
   * of a boot). Guarded by lock like the other lockout fields. */
  bool lockout_persist_armed;
  /* Set by init's restore when the persisted record was armed; consumed by
   * the first match cycle, which emits SECURITY_LOCKOUT at CRITICAL from
   * outside the lock. The emission cannot ride init itself (the event router
   * is not guaranteed running yet), but skipping it entirely would leave
   * subscribers reporting no alarm while scans are refused, and would pair
   * the eventual NORMAL clear with no CRITICAL - breaking the
   * security-event-unification pairing both the alarm state and the audit
   * trail depend on. */
  bool lockout_restore_announce_pending;
  sdf_services_admin_action_t pending_admin_action;
  int64_t pending_admin_action_start_us;
  /* Target carried by the remote user-management actions DELETE_USER,
   * REMOTE_ENROLL and RENAME_USER (companion-user-mgmt). Read by
   * execute_admin_action() when the authorizing scan claims the action. */
  uint16_t pending_admin_action_user_id;
  uint8_t pending_admin_action_permission;
  char pending_admin_action_name[SDF_STORAGE_WEB_USER_NAME_MAX];
  /* Resolution of the most recent remote delete/enroll action, recorded by
   * execute_admin_action() (authorized path), try_claim_admin_action()
   * (denied) and the admin-task timeout sweep (timed out), and popped once
   * by sdf_app via sdf_services_take_um_action_result(). Target id and
   * permission are snapshotted at record time, not read back from the
   * pending-action fields (which the next armed action overwrites).
   * Single slot: the single-pending-admin-action invariant guarantees at
   * most one of these actions can be in flight. */
  sdf_services_um_outcome_t um_action_result;
  uint16_t um_action_result_user_id;
  uint8_t um_action_result_permission;
  bool um_action_result_valid;
  bool permission_change_pending;
  uint16_t permission_change_user_id;
  uint8_t permission_change_permission;
  esp_err_t permission_change_result;
  /* Authoritative enrolled-user record: a bitmap (1 bit/user, IDs 1-10) plus
   * packed permissions (2 bits/user), loaded synchronously from NVS in
   * sdf_services_init() (before any task starts) and kept in sync with the
   * fingerprint sensor via synchronous NVS persistence on every successful
   * enroll/delete/clear/permission-change. See cache-enrolled-user-state.
   * enrolled_user_count is intentionally NOT a stored field - it's always
   * computed via sdf_services_enrolled_user_count() below, so it can't drift
   * from the bitmap it's derived from. */
  uint16_t enrolled_user_bmp;
  uint8_t enrolled_perm_packed[3];

  /* Web Companion registration authorization */
  bool web_reg_auth_pending;
  char request_web_username[SDF_STORAGE_WEB_USER_NAME_MAX];
  uint8_t request_web_password_hash[SDF_STORAGE_WEB_USER_HASH_LEN];
  /* Fingerprint user id of the admin whose scan authorized this pending
   * registration (companion-identity). Owned state like the name/hash
   * above: never carried in an event payload. 0 = not captured yet. */
  uint16_t request_web_authorizing_user_id;
  int64_t web_reg_auth_start_us;
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
void sdf_services_pulse_pending_action_led(sdf_services_admin_action_t action);

/* True for the remote user-management actions (DELETE_USER, REMOTE_ENROLL)
 * whose denial and timeout must resolve immediately with a named outcome
 * (companion-user-mgmt "Pending BLE-Originated Admin Actions Always
 * Resolve"). */
bool sdf_services_um_action_is_remote(sdf_services_admin_action_t action);
/* Records TIMEOUT as the outcome of a timed-out remote user-management
 * action. Callable with or without the lock held (the admin-task timeout
 * sweep calls it while holding s_state.lock). */
void sdf_services_record_um_action_timeout_locked(sdf_services_admin_action_t action);

/* Task declarations */
void sdf_match_task(void *arg);
void sdf_enroll_task(void *arg);
void sdf_admin_task(void *arg);
/* Holds the fingerprint sensor powered across a whole enrolment flow, or
 * releases it. Every acquire is paired with a release on each terminal path:
 * enrolment complete, enrolment failed, gate denied, gate timed out. */
void sdf_services_fp_hold_power(bool hold);

void sdf_enroll_task_wake(void);

/* Posts a run-the-next-step request to the enroll task's queue. See the
 * definition for why starting the state machine is not enough on its own. */
void sdf_enroll_task_run_step_soon(void);
void sdf_admin_task_wake(void);
esp_err_t sdf_match_task_init_subscriptions(void);
esp_err_t sdf_admin_task_init_subscriptions(void);
esp_err_t sdf_enroll_task_init_subscriptions(void);
esp_err_t sdf_match_task_init_queue(void);
void sdf_match_task_deinit_queue(void);
esp_err_t sdf_admin_task_init_queue(void);
void sdf_admin_task_deinit_queue(void);
esp_err_t sdf_enroll_task_init_queue(void);
void sdf_enroll_task_deinit_queue(void);

/* Runs one match cycle synchronously on the caller's task - the exact body
 * sdf_match_task executes per dispatched event. Host (linux target) tests
 * use it, behind the mock UART's scripted sensor replies, to drive the
 * lockout persistence transitions (entry / expiry / successful-match clear)
 * without hardware. Not called by production code. */
void sdf_match_task_run_cycle_for_test(void);

/* Boot-time lockout restore core (persist-biometric-lockout), called by
 * sdf_services_init() with s_state.lock held and before any task exists.
 * now_us is injected so host tests can pin the "full fresh duration from
 * boot" arithmetic deterministically. Missing/unreadable records resolve to
 * "not locked out"; the unreadable case is logged. */
void sdf_services_restore_lockout_locked(int64_t now_us);

/* Button handling */
esp_err_t sdf_button_init(void);
esp_err_t sdf_button_deinit(void);

/* Setup-phase button dispatch: executes an admin action directly via the
 * configured admin_action_cb without entering the pending-admin-action
 * wait. Used by the factory-reset gesture (no Admin fingerprint required -
 * see the device-setup-phase spec). */
void sdf_button_execute_direct(sdf_services_admin_action_t action);

/* Setup-phase module (sdf_services_setup.c) - host-test hooks. */
void sdf_services_setup_phase_reset_for_test(void);
void sdf_services_setup_phase_boot_arm(void);
/* Time-injected cores of the public notifications, so host tests can drive
 * the arm window / deadline / idle clocks deterministically. */
void sdf_services_setup_phase_arm_at(int64_t now_us);
void sdf_services_setup_phase_notify_connected_at(uint16_t conn_handle,
                                                  int64_t now_us);
void sdf_services_setup_phase_notify_disconnected_at(int64_t now_us);
void sdf_services_setup_phase_notify_gatt_activity_at(int64_t now_us);

/* Zeros the in-RAM enrolled-user cache and persists the zeroed record to
 * NVS (sdf_services.c). Used by the setup-phase timeout wipe, which cannot
 * rely on a whole-flash erase having already covered the persisted record. */
void sdf_services_reset_enrolled_user_cache(void);

/* Admin action dispatch, exposed so host (linux target) unit tests can drive it
 * directly without real hardware - see test_sdf_services.c. */
void sdf_button_dispatch_action(sdf_services_admin_action_t action);
void sdf_services_dispatch_admin_action(sdf_services_admin_action_t action);

/* Task start/stop */
esp_err_t sdf_services_start_tasks(void);
esp_err_t sdf_services_stop_tasks(void);

/* Writes the current s_state.enrolled_user_bmp/enrolled_perm_packed to NVS,
 * retrying with backoff on failure. Callable only while s_state.lock is
 * already held (see cache-enrolled-user-state design.md's synchronous
 * ordered persistence algorithm). Returns ESP_OK once the write lands, or
 * the last error observed once retries are exhausted. */
esp_err_t sdf_services_persist_enrolled_users_locked(void);

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

/* Enrolled-user count is always derived from the bitmap, never stored
 * independently - see enrolled_user_bmp's doc comment above. */
static inline size_t sdf_services_enrolled_user_count(uint16_t enrolled_user_bmp)
{
    return (size_t)__builtin_popcount((unsigned int)enrolled_user_bmp);
}

/* Pack sensor query results into compact bitmap + packed permissions */
void sdf_services_pack_user_list(const uint16_t *user_ids,
                                        const uint8_t *permissions,
                                        size_t count,
                                        uint16_t *bmp,
                                        uint8_t *perm_packed);

#endif /* SDF_SERVICES_INTERNAL_H */
