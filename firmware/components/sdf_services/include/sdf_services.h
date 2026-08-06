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
} sdf_services_admin_action_t;

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
 * sdf_services_web_auth.c. */

/* Login verification. Caller (sdf_ble_companion) already looked the user up
 * via sdf_storage_web_user_find_by_name(); this just isolates the
 * constant-time comparison so it's independently testable, including with
 * crafted mismatched-length / all-zero inputs. */
bool sdf_services_web_auth_verify_login(const sdf_storage_web_user_t *user,
                                         const uint8_t *submitted_hash,
                                         size_t hash_len);

/* Registration outcome. Mirrors sdf_app_on_web_reg_auth_result's logic minus
 * the actual sdf_storage_web_user_save() call and slot selection (still
 * sdf_app's job). */
typedef struct {
  bool should_persist;              /* false on denial/timeout */
  sdf_storage_web_user_t user;      /* populated only if should_persist */
  bool reply_authorized;            /* value to pass to sdf_ble_companion_reply_auth() */
} sdf_services_web_auth_registration_decision_t;

sdf_services_web_auth_registration_decision_t sdf_services_web_auth_decide_registration(
    const char *username, const uint8_t *password_hash, size_t hash_len,
    uint8_t permission, bool admin_authorized);

/* Timeout/reject unlatch guard. Trivial today (action == WEB_REG_AUTH &&
 * result != ESP_OK), but made explicit and tested so a future admin-action
 * type addition can't silently break the "always resolve the pending BLE
 * client" guarantee sdf_app_on_admin_action_complete's comment warns about. */
bool sdf_services_web_auth_should_resolve_on_action_complete(
    sdf_services_admin_action_t action, esp_err_t result);

#endif /* SDF_SERVICES_H */
