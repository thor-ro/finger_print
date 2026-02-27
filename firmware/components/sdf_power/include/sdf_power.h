#ifndef SDF_POWER_H
#define SDF_POWER_H

#include "sdkconfig.h"
#include <stdbool.h>
#include <stdint.h>

#ifndef CONFIG_IDF_TARGET_LINUX
#include "driver/gpio.h"
#else
#include "hal/gpio_types.h"
#endif
#include "esp_err.h"

#ifndef CONFIG_IDF_TARGET_LINUX
#include "sdf_nuki_ble_transport.h"
#else
typedef struct sdf_nuki_ble_transport_t sdf_nuki_ble_transport_t;
#endif

typedef enum {
  SDF_POWER_WAKE_REASON_NONE = 0,
  SDF_POWER_WAKE_REASON_TIMER = 1,
  SDF_POWER_WAKE_REASON_FINGERPRINT = 2,
  SDF_POWER_WAKE_REASON_OTHER = 3,
} sdf_power_wake_reason_t;

typedef bool (*sdf_power_busy_cb)(void *ctx);
typedef void (*sdf_power_wake_cb)(void *ctx, sdf_power_wake_reason_t reason);
typedef int (*sdf_power_battery_cb)(void *ctx);

typedef struct {
  sdf_nuki_ble_transport_t *ble_transport;
  gpio_num_t fingerprint_wake_gpio;
  uint32_t checkin_interval_ms;
  uint32_t idle_before_sleep_ms;
  uint32_t post_wake_guard_ms;
  uint32_t loop_interval_ms;
  uint32_t battery_report_interval_ms;
  bool enable_light_sleep;
  bool enable_ble_radio_gating;
  bool enable_deep_sleep_fallback;
  uint8_t battery_percent_default;
  sdf_power_busy_cb busy_cb;
  void *busy_ctx;
  sdf_power_wake_cb wake_cb;
  void *wake_ctx;
  sdf_power_battery_cb battery_cb;
  void *battery_ctx;
} sdf_power_manager_config_t;

void sdf_power_init(void);

void sdf_power_get_default_power_config(sdf_power_manager_config_t *config);
esp_err_t
sdf_power_init_power_manager(const sdf_power_manager_config_t *config);

bool sdf_power_power_manager_ready(void);
void sdf_power_mark_activity(void);

esp_err_t sdf_power_set_checkin_interval_ms(uint32_t checkin_interval_ms);
uint32_t sdf_power_get_checkin_interval_ms(void);

esp_err_t sdf_power_set_battery_percent(uint8_t battery_percent);
uint8_t sdf_power_get_battery_percent(void);

#endif /* SDF_POWER_H */
