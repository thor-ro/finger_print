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
} sdf_services_admin_action_t;

/* Device setup progress, derived from existing persisted state
 * (enrolled_user_count, sdf_storage_nuki_load()) rather than a dedicated
 * flag. See the "State-Dependent Single-Click Setup Action" requirement. */
typedef enum {
  SDF_SERVICES_SETUP_STATE_UNCLAIMED = 0,          /* no enrolled users yet */
  SDF_SERVICES_SETUP_STATE_CLAIMED_INCOMPLETE = 1, /* admin exists, Nuki not yet paired */
  SDF_SERVICES_SETUP_STATE_CLAIMED_COMPLETE = 2,   /* admin exists, Nuki credentials persisted */
} sdf_services_setup_state_t;

typedef void (*sdf_services_admin_action_cb)(
    void *ctx, sdf_services_admin_action_t action);

typedef struct {
  sdf_fingerprint_driver_config_t fingerprint;
  uint32_t match_poll_interval_ms;
  uint32_t match_cooldown_ms;
  uint32_t failed_attempt_threshold;
  uint32_t failed_attempt_window_ms;
  uint32_t lockout_duration_ms;
  sdf_services_admin_action_cb admin_action_cb;
  void *admin_action_ctx;
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

esp_err_t sdf_services_delete_user(uint16_t user_id);
esp_err_t sdf_services_clear_all_users(void);
esp_err_t sdf_services_query_users(uint16_t *user_ids, uint8_t *permissions,
                                   size_t *count, size_t max_count);
esp_err_t sdf_services_change_user_permission(uint16_t user_id,
                                              uint8_t permission);

esp_err_t sdf_services_request_admin_action(sdf_services_admin_action_t action);

void sdf_services_trigger_low_battery_warning(void);

esp_err_t sdf_services_reset_state(void);

/* Enrollment API - event-driven */
esp_err_t sdf_services_request_enrollment(uint16_t user_id, uint8_t permission);
bool sdf_services_is_enrollment_active(void);
sdf_enrollment_sm_t sdf_services_get_enrollment_state(void);

/* Admin Action API */
esp_err_t sdf_services_request_admin_action(sdf_services_admin_action_t action);

/* Setup-state query, used by the button dispatch and the BLE-triggered Nuki
 * re-pair request to decide whether the device is unclaimed, claimed with
 * setup incomplete, or claimed with setup already complete. */
sdf_services_setup_state_t sdf_services_get_setup_state(void);

/* Web Companion Auth API */
esp_err_t sdf_services_set_web_reg_auth(const char *username,
                                         const uint8_t *password_hash,
                                         size_t hash_len);
esp_err_t sdf_services_get_web_reg_auth(char *username, size_t username_max,
                                         uint8_t *permission);
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

/* Placeholder iteration count pending an on-device PBKDF2 benchmark (see
 * design.md Open Questions / tasks.md 6.1); targets the OWASP
 * PBKDF2-HMAC-SHA256 baseline (~210k) until that benchmark says otherwise. */
#define SDF_SERVICES_WEB_AUTH_PBKDF2_ITERATIONS 210000u

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
 * the actual sdf_storage_web_user_save() call and slot selection (still
 * sdf_app's job). Salt is caller-generated (see comment above); this
 * function runs the PBKDF2 stretch (via
 * sdf_services_web_auth_stretch_credential) and persists only the salt and
 * stretched credential, never the raw received hash. */
typedef struct {
  bool should_persist;              /* false on denial/timeout */
  sdf_storage_web_user_t user;      /* populated only if should_persist */
  bool reply_authorized;            /* value to pass to sdf_ble_companion_reply_auth() */
} sdf_services_web_auth_registration_decision_t;

sdf_services_web_auth_registration_decision_t sdf_services_web_auth_decide_registration(
    const char *username, const uint8_t *password_hash, size_t hash_len,
    const uint8_t salt[SDF_STORAGE_WEB_USER_SALT_LEN],
    uint8_t permission, bool admin_authorized);

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
