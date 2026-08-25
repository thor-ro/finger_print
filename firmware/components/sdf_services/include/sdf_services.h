#ifndef SDF_SERVICES_H
#define SDF_SERVICES_H

#include "sdkconfig.h"
#include <stdbool.h>
#include <stdint.h>

#ifndef CONFIG_IDF_TARGET_LINUX
#include "driver/gpio.h"
#else
#include "hal/gpio_types.h"
#endif
#include "esp_err.h"

#include "fingerprint.h"
#include "sdf_state_machines.h"
#include "sdf_storage.h"

typedef enum {
  SDF_SERVICES_ADMIN_ACTION_NONE = 0,
  SDF_SERVICES_ADMIN_ACTION_ENROLL = 1,
  SDF_SERVICES_ADMIN_ACTION_NUKI_PAIR = 2,
  SDF_SERVICES_ADMIN_ACTION_ZB_JOIN = 3,
  SDF_SERVICES_ADMIN_ACTION_FACTORY_RESET = 4,
  SDF_SERVICES_ADMIN_ACTION_CHANGE_PERMISSION = 5,
  SDF_SERVICES_ADMIN_ACTION_ENROLL_ADMIN = 6,
  SDF_SERVICES_ADMIN_ACTION_WEB_REG_AUTH = 7,
  SDF_SERVICES_ADMIN_ACTION_NUKI_REPAIR = 8,
  /* Button-triggered (double-click), not BLE-triggered like WEB_REG_AUTH/
   * NUKI_REPAIR above: opens the BLE Companion Service's Admin-Fingerprint-
   * Gated Device Pairing Window (see sdf_ble_companion_open_pairing_window())
   * once an Admin fingerprint authorizes it. No BLE client to reply to. */
  SDF_SERVICES_ADMIN_ACTION_BLE_PAIRING_WINDOW = 9,
  /* Remote (companion-originated) user-management actions
   * (companion-user-mgmt). Both carry their target in the pending-action
   * state (user id, and permission for REMOTE_ENROLL); both resolve through
   * the ordinary admin-fingerprint gate and record a named
   * sdf_services_um_outcome_t for the requesting client (see
   * sdf_services_take_um_action_result()). */
  SDF_SERVICES_ADMIN_ACTION_DELETE_USER = 10,
  SDF_SERVICES_ADMIN_ACTION_REMOTE_ENROLL = 11,
  /* Rename is a mutating verb too, and under companion-identity the name
   * is a login identifier - so it joins the same admin-fingerprint gate
   * carrying its target id and the new name. */
  SDF_SERVICES_ADMIN_ACTION_RENAME_USER = 12,
} sdf_services_admin_action_t;

/* Named outcome of a user-management verb (companion-user-mgmt). Produced
 * where the condition is known - inside sdf_services - rather than decoded
 * by callers from an esp_err_t whose values are shared across unrelated
 * conditions (last-admin vs busy vs uninitialised all used to be
 * ESP_ERR_INVALID_STATE). The first nine values are carried on the wire as
 * the reply's "result" string exactly as spelled here (lowercase); FAILED
 * and UNAVAILABLE are firmware-internal only and render as a generic
 * failure at any surface. */
typedef enum {
  SDF_SERVICES_UM_OK = 0,           /* the verb completed */
  SDF_SERVICES_UM_NOT_FOUND = 1,    /* no such enrolled user */
  SDF_SERVICES_UM_ID_OCCUPIED = 2,  /* enrolment target id already enrolled */
  SDF_SERVICES_UM_LAST_ADMIN = 3,   /* refused: would leave no admin */
  SDF_SERVICES_UM_NAME_TAKEN = 4,   /* refused: another user holds that name */
  SDF_SERVICES_UM_BUSY = 5,         /* another action/permission-change/enrolment in flight */
  SDF_SERVICES_UM_DENIED = 6,       /* the authorizing scan did not match an admin */
  SDF_SERVICES_UM_TIMEOUT = 7,      /* no authorizing scan within the window */
  SDF_SERVICES_UM_INVALID = 8,      /* malformed request / out-of-range field */
  /* Firmware-internal outcomes below are never carried on the wire. */
  SDF_SERVICES_UM_FAILED = 9,       /* sensor or storage operation failed */
  SDF_SERVICES_UM_UNAVAILABLE = 10, /* services not initialised */
} sdf_services_um_outcome_t;

/* Wire-stable name of an outcome ("ok", "not_found", ...) - the exact
 * string the companion protocol carries in a reply's "result" field. */
const char *sdf_services_um_outcome_name(sdf_services_um_outcome_t outcome);

/* Device setup progress. Completion is latched (persisted via
 * sdf_storage_setup_complete_save()); the intermediate steps remain derived
 * from enrolled-user state and persisted Nuki credentials, since they drive
 * only wizard step selection and are not security-bearing. See the
 * device-setup-phase capability (openspec/changes/app-guided-first-time-
 * setup). */
/* Wizard progress, in step order. Every intermediate value is derived from
 * independently-mutable state and drives only wizard step selection; only
 * COMPLETE is security-bearing, and it comes from the latch.
 *
 * REGISTERED exists so ADMIN_ENROLLED is not ambiguous between "still needs
 * an account" and "needs Nuki pairing" - the wizard resumes on this value
 * after a reconnect, and sdf_app's completion check requires the terminal
 * pre-completion value rather than spot-checking each prerequisite. */
typedef enum {
  SDF_SERVICES_SETUP_STATE_NOT_STARTED = 0,   /* no enrolled users yet */
  SDF_SERVICES_SETUP_STATE_ADMIN_ENROLLED = 1, /* admin exists, no account yet */
  SDF_SERVICES_SETUP_STATE_REGISTERED = 2,    /* account persisted, Nuki not yet paired */
  SDF_SERVICES_SETUP_STATE_NUKI_PAIRED = 3,   /* Nuki credentials persisted, not finished */
  SDF_SERVICES_SETUP_STATE_COMPLETE = 4,      /* setup-completion latch set */
} sdf_services_setup_state_t;

/* Setup-phase bounds (compile-time, see the device-setup-phase spec):
 * - ARM_WINDOW: how long the device advertises openly with no client
 *   connected, from the moment the phase was armed.
 * - DEADLINE: how long a user has from the first accepted connection.
 *   Never extended by activity, progress, disconnection or reconnection -
 *   only a physical button press restarts it.
 * - CONN_IDLE: silence bound for a single setup connection; expiring drops
 *   only the connection, never the deadline or any state. */
#define SDF_SETUP_ARM_WINDOW_MS 300000u
#define SDF_SETUP_DEADLINE_MS 600000u
#define SDF_SETUP_CONN_IDLE_MS 120000u

/* Actions requested by sdf_services_setup_phase_poll() when a setup-phase
 * timer expires. The caller (the admin task loop) executes them: WIPE_AND_
 * STOP erases partial setup state, disarms the phase and emits
 * SDF_EVENT_ROUTER_SETUP_PHASE(timeout) so the BLE side clears bonds,
 * terminates the setup connection and stops advertising; DROP_IDLE_CONN
 * emits SDF_EVENT_ROUTER_SETUP_PHASE(idle_drop) so the BLE side terminates
 * only the silent connection and re-arms advertising. */
typedef enum {
  SDF_SERVICES_SETUP_POLL_NONE = 0,
  SDF_SERVICES_SETUP_POLL_WIPE_AND_STOP = 1,
  SDF_SERVICES_SETUP_POLL_DROP_IDLE_CONN = 2,
} sdf_services_setup_poll_result_t;

typedef void (*sdf_services_admin_action_cb)(
    void *ctx, sdf_services_admin_action_t action);

/* Published by the fingerprint path after it performed sensor I/O for its
 * own reasons (a match scan or an enrolment step): ready=true when the
 * sensor answered, false when it timed out or errored. Never invoked on
 * behalf of a reader - health reads must not probe the sensor. May be NULL.
 * Runs on the match/enroll task context; the implementer must not block. */
typedef void (*sdf_services_fingerprint_ready_cb)(void *ctx, bool ready);

typedef struct {
  sdf_fingerprint_driver_config_t fingerprint;
  uint32_t match_poll_interval_ms;
  uint32_t match_cooldown_ms;
  uint32_t failed_attempt_threshold;
  uint32_t failed_attempt_window_ms;
  uint32_t lockout_duration_ms;
  sdf_services_admin_action_cb admin_action_cb;
  void *admin_action_ctx;
  sdf_services_fingerprint_ready_cb fingerprint_ready_cb;
  void *fingerprint_ready_ctx;
  gpio_num_t wake_gpio;
  gpio_num_t power_en_gpio;
  gpio_num_t enrollment_btn_gpio;
  gpio_num_t ws2812_led_gpio;
  int battery_adc_pin;
} sdf_services_config_t;

void sdf_services_get_default_config(sdf_services_config_t *config);

esp_err_t sdf_services_init(const sdf_services_config_t *config);
bool sdf_services_is_ready(void);

/* New task-based API */
esp_err_t sdf_services_start_tasks(void);
esp_err_t sdf_services_stop_tasks(void);

esp_err_t sdf_services_clear_all_users(void);
esp_err_t sdf_services_query_users(uint16_t *user_ids, uint8_t *permissions,
                                   size_t *count, size_t max_count);

/* One serialized shape for the enrolled-user list (companion-user-mgmt "The
 * User List Has One Serialized Shape"): a JSON array
 * `[{"id":1,"perm":3,"name":"Alice"},{"id":5,"perm":1}]`. Every consumer
 * that reports the list - the Zigbee report and the companion reply -
 * builds its entries and calls this same serializer, so neither can drift
 * into its own field names or value shapes. A user holding no name is
 * represented by the absence of the name, never an empty string. */
/* Capacity of any consumer's user-list staging: ids run 1..MAX. */
#define SDF_SERVICES_USER_LIST_MAX SDF_FINGERPRINT_USER_ID_MAX

typedef struct {
  uint16_t id;
  uint8_t permission;
  const char *name; /* NULL or empty omits the "name" field */
} sdf_services_user_list_entry_t;

/* Serializes `count` entries into `buf` (at most buf_size bytes including
 * the terminating NUL). Returns the number of bytes written excluding the
 * NUL, or 0 when the buffer was too small - callers treat 0 as "does not
 * fit" rather than as an empty list, which always serializes to at least
 * "[]" (2 bytes). */
size_t sdf_services_format_user_list(
    const sdf_services_user_list_entry_t *entries, size_t count, char *buf,
    size_t buf_size);

/* User-management verbs report a named outcome (sdf_services_um_outcome_t)
 * decided here, where each condition is known - callers render what these
 * return rather than decoding an esp_err_t from their own context
 * (companion-user-mgmt). */

/* Deletes an enrolled user. Refuses an unenrolled id with NOT_FOUND and the
 * only enrolled admin with LAST_ADMIN, both before any sensor traffic. */
sdf_services_um_outcome_t sdf_services_delete_user(uint16_t user_id);

/* Changes an enrolled user's permission. Arms the CHANGE_PERMISSION admin
 * action and waits up to SDF_SERVICES_PERMISSION_CHANGE_WAIT_MS for the
 * authorizing Admin fingerprint; a scan that never arrives resolves as
 * TIMEOUT, a non-admin match is refused by the gate (the caller observes
 * TIMEOUT - the gate itself reports DENIED to its own resolution path).
 * Written for a task that may block: do not call from the NimBLE host
 * task. */
sdf_services_um_outcome_t sdf_services_change_user_permission(uint16_t user_id,
                                                              uint8_t permission);

/* True when `user_id` is currently enrolled AND its cached permission is
 * admin (3). Reads only the in-memory enrolled-user cache - this is the
 * live authority lookup behind the companion session gate
 * (companion-identity "Session Authority Is Derived Live From The Bound
 * User"): a demotion or deletion of the bound user takes effect on open
 * sessions at their next decision, with no cascade. */
/* Live enrolment check regardless of permission level: used by the Status
 * characteristic's admission rule, where any authenticated bound user may
 * read but a deleted bound user must lose access (companion-device-health).
 * Mirrors user_is_enrolled_admin()'s live-resolution contract. */
bool sdf_services_user_is_enrolled(uint16_t user_id);
bool sdf_services_user_is_enrolled_admin(uint16_t user_id);

/* Resolves which enrolled user currently holds `name`, scanning every
 * unified per-user record regardless of whether it holds a credential -
 * names are unique across all enrolled users, not just account holders.
 * Returns ESP_OK and the holder's id when found, ESP_ERR_NOT_FOUND
 * otherwise. */
esp_err_t sdf_services_find_name_holder(const char *name, uint16_t *holder_id_out);

/* Sets (renames) an enrolled user's name inside their unified record,
 * preserving any stored credential. Refuses an unenrolled id with
 * NOT_FOUND and a name already held by a different enrolled user with
 * NAME_TAKEN, leaving every record unchanged. */
sdf_services_um_outcome_t sdf_services_set_user_name(uint16_t user_id, const char *name);

esp_err_t sdf_services_request_admin_action(sdf_services_admin_action_t action);

void sdf_services_trigger_low_battery_warning(void);

esp_err_t sdf_services_reset_state(void);

/* Enrollment API - event-driven */
/* Arms the enrolment state machine directly (no admin-fingerprint gate):
 * for the physical button path, the Zigbee programming commands and the
 * setup phase's first enrolment, which have their own authorization. A
 * target id that is already enrolled is refused with ID_OCCUPIED here -
 * the check lives in the services layer so every caller gets it - and a
 * busy device (enrolment or admin action already in flight) with BUSY. */
sdf_services_um_outcome_t sdf_services_request_enrollment(uint16_t user_id,
                                                          uint8_t permission);
bool sdf_services_is_enrollment_active(void);
sdf_enrollment_sm_t sdf_services_get_enrollment_state(void);

/* Remote (companion-originated) user-management actions: arms the pending
 * admin action (carrying the target id/permission) and returns immediately.
 * The action resolves through the admin-fingerprint gate; its outcome is
 * recorded and retrieved with sdf_services_take_um_action_result().
 * Immediate refusals - guards evaluated BEFORE the gate is armed, so an
 * impossible request never asks anyone to scan - return the named outcome
 * without arming anything; OK means the gate is now armed and waiting. */

sdf_services_um_outcome_t sdf_services_request_delete_user(uint16_t user_id);
sdf_services_um_outcome_t sdf_services_request_remote_enrollment(
    uint16_t user_id, uint8_t permission);
sdf_services_um_outcome_t sdf_services_request_rename_user(uint16_t user_id,
                                                           const char *name);

/* Pops the outcome of the most recently resolved DELETE_USER or
 * REMOTE_ENROLL admin action (authorized-executed, denied, or timed out).
 * Returns false when no unresolved result is waiting. */
bool sdf_services_take_um_action_result(uint16_t *user_id_out,
                                        uint8_t *permission_out,
                                        sdf_services_um_outcome_t *outcome_out);

/* Admin Action API */
esp_err_t sdf_services_request_admin_action(sdf_services_admin_action_t action);

/* Setup-state query, used by the button dispatch, the BLE-triggered Nuki
 * re-pair request and the Companion Service's setup-state characteristic to
 * decide where in the setup journey the device is. Completion comes from the
 * persisted latch; the intermediate states are derived. */
sdf_services_setup_state_t sdf_services_get_setup_state(void);

/* Setup-phase lifecycle (armed/disarmed + timers). The state is runtime-only:
 * a boot with the latch unset arms the phase (first boot of an unprovisioned
 * device, or the reboot that follows a factory reset); a timeout disarm does
 * not survive as persisted state either - only the button re-arms after a
 * lapse within the same power session. */
bool sdf_services_setup_phase_is_armed(void);
/* Arms/re-arms the phase, restarting the arm window; the setup deadline is
 * restarted too if it had already started (the button reclaim gesture), and
 * left unstarted otherwise. */
void sdf_services_setup_phase_arm(void);
/* Disarms without wiping - used by the completion path once the latch is set. */
void sdf_services_setup_phase_disarm(void);
/* Notifications from the BLE layer about the single setup connection. */
void sdf_services_setup_phase_notify_connected(uint16_t conn_handle);
void sdf_services_setup_phase_notify_disconnected(uint16_t conn_handle);
void sdf_services_setup_phase_notify_gatt_activity(void);
/* Timer sweep. Returns the action the caller must execute (see
 * sdf_services_setup_poll_result_t). `now_us` is esp_timer_get_time()-style
 * microsecond time, passed in so host tests can drive the clocks directly. */
sdf_services_setup_poll_result_t sdf_services_setup_phase_poll(int64_t now_us);
/* Executes SDF_SERVICES_SETUP_POLL_DROP_IDLE_CONN: emits the idle-drop event
 * so the BLE side terminates only the silent setup connection and re-arms
 * advertising. The deadline, all partial state and the armed phase stay
 * untouched. */
void sdf_services_setup_phase_idle_drop(void);
/* Executes SDF_SERVICES_SETUP_POLL_WIPE_AND_STOP's device-side half: erases
 * enrolled templates, web accounts, admission records and partial Nuki
 * credentials, disarms the phase and emits the setup-timeout event for the
 * BLE side (bond clearing + advertising stop). Template-erase failure is
 * logged and non-fatal. */
void sdf_services_setup_phase_timeout_wipe(void);
/* The setup-phase button gesture: reclaim-and-re-arm. Emits the reclaim
 * event so the BLE side terminates the current setup connection and
 * re-arms advertising, restarts both timers, and sets no pending admin
 * action. */
void sdf_services_setup_phase_button_reclaim(void);
/* True when the setup phase owns button presses (latch unset). */
bool sdf_services_setup_phase_owns_buttons(void);

/* Web Companion Auth API */
esp_err_t sdf_services_set_web_reg_auth(const char *username,
                                         const uint8_t *password_hash,
                                         size_t hash_len);
esp_err_t sdf_services_get_web_reg_auth(char *username, size_t username_max,
                                         uint16_t *authorizing_user_id);
esp_err_t sdf_services_get_web_reg_password_hash(uint8_t *password_hash,
                                                  size_t hash_len);
void sdf_services_clear_web_reg_auth(void);

/* Web Companion Auth decisions - pure functions, no I/O, no locks. See
 * sdf_services_web_auth.c. Salt and nonce values are always supplied by the
 * caller (generated via esp_fill_random in the I/O layer - sdf_app.c for
 * REGISTER's salt, sdf_ble_companion.c for LOGIN_INIT's nonce) rather than
 * generated here, so these functions stay deterministic and independently
 * unit-testable. */

#define SDF_SERVICES_WEB_AUTH_NONCE_LEN 16
#define SDF_SERVICES_WEB_AUTH_RESPONSE_LEN 32 /* HMAC-SHA256 output */

/* Iteration count for the one-time, device-side REGISTER credential stretch
 * (see design.md Open Questions / tasks.md 6.1). The OWASP baseline of
 * ~210k iterations was benchmarked on a real ESP32-C6 (single, unaccelerated
 * RISC-V core) at ~17.75s per call - this fully blocks the CPU (no internal
 * yield points in mbedtls_pkcs5_pbkdf2_hmac_ext) for the whole duration,
 * which both makes REGISTER feel hung and starves the CPU0 idle task well
 * past the default 5s task-watchdog timeout, tripping a watchdog panic
 * before the call can finish. 10,000 iterations (the NIST SP 800-132
 * minimum) measures at ~845ms on the same hardware - a comfortable margin
 * under the watchdog timeout and a reasonable one-time REGISTER-side wait,
 * at the cost of weaker stretching than the OWASP baseline. Acceptable here
 * because REGISTER is already gated by physical BLE proximity plus an admin
 * fingerprint enrollment, and online LOGIN guesses are separately
 * rate-limited by the bond lockout counter. */
#define SDF_SERVICES_WEB_AUTH_PBKDF2_ITERATIONS 10000u

/* PBKDF2-HMAC-SHA256 credential stretch. Used once, device-side, at
 * REGISTER to turn the received SHA256(password) into the value persisted
 * as sdf_storage_web_user_t.stretched_credential. */
esp_err_t sdf_services_web_auth_stretch_credential(
    const uint8_t *received_hash, size_t received_hash_len,
    const uint8_t salt[SDF_STORAGE_WEB_USER_SALT_LEN],
    uint32_t iteration_count,
    uint8_t stretched_credential_out[SDF_STORAGE_WEB_USER_STRETCHED_LEN]);

/* LOGIN_VERIFY decision: does the submitted response match
 * HMAC-SHA256(stretched_credential, nonce)? Replaces the old
 * sdf_services_web_auth_verify_login (raw hash comparison). Compared via
 * mbedtls_ct_memcmp, same as before. */
bool sdf_services_web_auth_verify_response(
    const uint8_t stretched_credential[SDF_STORAGE_WEB_USER_STRETCHED_LEN],
    const uint8_t *nonce, size_t nonce_len,
    const uint8_t *submitted_response, size_t response_len);

/* LOGIN_INIT challenge fields returned to the client. Same shape whether the
 * account exists or not - see make_login_challenge/make_pseudo_challenge. */
typedef struct {
  uint8_t salt[SDF_STORAGE_WEB_USER_SALT_LEN];
  uint32_t iteration_count;
  uint8_t nonce[SDF_SERVICES_WEB_AUTH_NONCE_LEN];
} sdf_services_web_auth_challenge_t;

/* Challenge for a username with a stored account: real salt, real (fixed)
 * iteration count, caller-supplied fresh nonce. */
sdf_services_web_auth_challenge_t sdf_services_web_auth_make_login_challenge(
    const sdf_storage_web_user_t *user,
    const uint8_t nonce[SDF_SERVICES_WEB_AUTH_NONCE_LEN]);

/* Challenge for a username with no stored account: deterministic pseudo-salt
 * derived as HMAC(pseudo_salt_key, username), same iteration count, and the
 * same caller-supplied fresh nonce - so the response is indistinguishable in
 * shape from a real account's, and no LOGIN_VERIFY can ever match it. */
sdf_services_web_auth_challenge_t sdf_services_web_auth_make_pseudo_challenge(
    const uint8_t pseudo_salt_key[SDF_STORAGE_WEB_PSEUDO_SALT_KEY_LEN],
    const char *username,
    const uint8_t nonce[SDF_SERVICES_WEB_AUTH_NONCE_LEN]);

/* Registration outcome. Mirrors sdf_app_on_web_reg_auth_result's logic minus
 * the actual sdf_storage_web_user_save() call (still sdf_app's job). Salt is
 * caller-generated (see comment above); this function runs the PBKDF2 stretch
 * (via sdf_services_web_auth_stretch_credential) and persists only the salt
 * and stretched credential, never the raw received hash.
 *
 * The credential binds to `authorizing_user_id`, the fingerprint user whose
 * scan authorized the registration (companion-identity): a user that already
 * holds an account gets its credential replaced in place, and a registration
 * with no captured authorizer - or one whose submitted name is already held
 * by a different enrolled user (`name_available` false) - is refused with
 * nothing persisted. */
typedef struct {
  bool should_persist;              /* false on denial/refusal/timeout */
  uint16_t user_id;                 /* bound fingerprint user id, iff should_persist */
  sdf_storage_web_user_t user;      /* populated only if should_persist */
  bool reply_authorized;            /* value to pass to sdf_ble_companion_reply_auth() */
} sdf_services_web_auth_registration_decision_t;

sdf_services_web_auth_registration_decision_t sdf_services_web_auth_decide_registration(
    const char *name, const uint8_t *password_hash, size_t hash_len,
    const uint8_t salt[SDF_STORAGE_WEB_USER_SALT_LEN],
    uint16_t authorizing_user_id, bool admin_authorized,
    bool name_available);

/* Timeout/reject unlatch guard. Trivial today (action == WEB_REG_AUTH &&
 * result != ESP_OK), but made explicit and tested so a future admin-action
 * type addition can't silently break the "always resolve the pending BLE
 * client" guarantee sdf_app_on_admin_action_complete's comment warns about. */
bool sdf_services_web_auth_should_resolve_on_action_complete(
    sdf_services_admin_action_t action, esp_err_t result);

/* Same guarantee as sdf_services_web_auth_should_resolve_on_action_complete(),
 * shared across every BLE-triggered admin action that routes its result back
 * to an originating GATT connection (Nuki re-pair, Enroll-Admin, Zigbee
 * Join): true only if `action` is one of those and completed with a non-OK
 * result (denial or timeout), so the pending BLE client is never left
 * waiting indefinitely. A single shared function rather than one per action
 * so adding a future BLE-triggered action can't forget this guarantee. See
 * sdf_services_web_auth.c. */
bool sdf_services_ble_admin_action_should_resolve_on_action_complete(
    sdf_services_admin_action_t action, esp_err_t result);

#endif /* SDF_SERVICES_H */
