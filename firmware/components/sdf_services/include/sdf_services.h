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
#include "sdf_services_enrollment.h"

typedef int (*sdf_services_unlock_cb)(void *ctx, uint16_t user_id);

typedef enum {
  SDF_SERVICES_ADMIN_ACTION_NONE = 0,
  SDF_SERVICES_ADMIN_ACTION_ENROLL = 1,
  SDF_SERVICES_ADMIN_ACTION_NUKI_PAIR = 2,
  SDF_SERVICES_ADMIN_ACTION_ZB_JOIN = 3,
  SDF_SERVICES_ADMIN_ACTION_FACTORY_RESET = 4,
  SDF_SERVICES_ADMIN_ACTION_CHANGE_PERMISSION = 5,
  SDF_SERVICES_ADMIN_ACTION_ENROLL_ADMIN = 6,
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
  int battery_adc_pin;
} sdf_services_config_t;

void sdf_services_get_default_config(sdf_services_config_t *config);

esp_err_t sdf_services_init(const sdf_services_config_t *config);
bool sdf_services_is_ready(void);

esp_err_t sdf_services_delete_user(uint16_t user_id);
esp_err_t sdf_services_clear_all_users(void);
esp_err_t sdf_services_query_users(uint16_t *user_ids, uint8_t *permissions,
                                   size_t *count, size_t max_count);
esp_err_t sdf_services_change_user_permission(uint16_t user_id,
                                              uint8_t permission);

void sdf_services_trigger_low_battery_warning(void);

#endif /* SDF_SERVICES_H */
