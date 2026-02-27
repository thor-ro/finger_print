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

#include "sdf_drivers.h"
#include "sdf_state_machines.h"

typedef int (*sdf_services_unlock_cb)(void *ctx, uint16_t user_id);
typedef void (*sdf_services_enrollment_cb)(void *ctx,
                                           const sdf_enrollment_sm_t *state);

typedef enum {
  SDF_SERVICES_ADMIN_ACTION_NONE = 0,
  SDF_SERVICES_ADMIN_ACTION_ENROLL = 1,
  SDF_SERVICES_ADMIN_ACTION_NUKI_PAIR = 2,
  SDF_SERVICES_ADMIN_ACTION_ZB_JOIN = 3,
  SDF_SERVICES_ADMIN_ACTION_FACTORY_RESET = 4,
} sdf_services_admin_action_t;

typedef void (*sdf_services_admin_action_cb)(
    void *ctx, sdf_services_admin_action_t action);

typedef enum {
  SDF_SERVICES_SECURITY_EVENT_MATCH_FAILED = 0,
  SDF_SERVICES_SECURITY_EVENT_LOCKOUT_ENTERED = 1,
  SDF_SERVICES_SECURITY_EVENT_LOCKOUT_CLEARED = 2,
  SDF_SERVICES_SECURITY_EVENT_MATCH_SUCCEEDED = 3
} sdf_services_security_event_type_t;

typedef struct {
  sdf_services_security_event_type_t type;
  uint16_t user_id;
  uint32_t failed_attempts;
  uint32_t lockout_remaining_ms;
} sdf_services_security_event_t;

typedef void (*sdf_services_security_event_cb)(
    void *ctx, const sdf_services_security_event_t *event);

typedef struct {
  sdf_fingerprint_driver_config_t fingerprint;
  uint32_t match_poll_interval_ms;
  uint32_t match_cooldown_ms;
  uint32_t failed_attempt_threshold;
  uint32_t failed_attempt_window_ms;
  uint32_t lockout_duration_ms;
  sdf_services_unlock_cb unlock_cb;
  void *unlock_ctx;
  sdf_services_enrollment_cb enrollment_cb;
  void *enrollment_ctx;
  sdf_services_admin_action_cb admin_action_cb;
  void *admin_action_ctx;
  sdf_services_security_event_cb security_event_cb;
  void *security_event_ctx;
  gpio_num_t wake_gpio;
  gpio_num_t power_en_gpio;
  gpio_num_t enrollment_btn_gpio;
  gpio_num_t ws2812_led_gpio;
} sdf_services_config_t;

void sdf_services_get_default_config(sdf_services_config_t *config);

esp_err_t sdf_services_init(const sdf_services_config_t *config);
bool sdf_services_is_ready(void);

esp_err_t sdf_services_request_enrollment(uint16_t user_id, uint8_t permission);
bool sdf_services_is_enrollment_active(void);
sdf_enrollment_sm_t sdf_services_get_enrollment_state(void);

esp_err_t sdf_services_delete_user(uint16_t user_id);
esp_err_t sdf_services_clear_all_users(void);
esp_err_t sdf_services_query_users(uint16_t *user_ids, uint8_t *permissions,
                                   size_t *count, size_t max_count);

void sdf_services_trigger_low_battery_warning(void);

#endif /* SDF_SERVICES_H */
