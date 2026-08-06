#include "sdf_power.h"
#include "sdf_platform.h"
#include "sdf_config.h"
#include "sdf_platform_sleep.h"
#include "sdf_power_policy.h"
#include "sdf_platform_power.h"

#include <string.h>

#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#ifndef CONFIG_IDF_TARGET_LINUX
#include "esp_task_wdt.h"
#endif
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#ifndef CONFIG_IDF_TARGET_LINUX
#include "driver/usb_serial_jtag.h"
#endif

#include "sdf_protocol_zigbee.h"
#include "sdf_event_router.h"

#ifdef SDF_POWER_TESTING
#define SDF_POWER_STATIC
#else
#define SDF_POWER_STATIC static
#endif

#define SDF_POWER_TASK_NAME "sdf_power"
#define SDF_POWER_TASK_STACK 4096
#define SDF_POWER_TASK_PRIORITY 4
#define SDF_POWER_LOCK_WAIT_MS 250u
#define SDF_POWER_CHECKIN_INTERVAL_MIN_MS 1000u
#define SDF_POWER_CHECKIN_INTERVAL_MAX_MS 600000u

static const char *TAG = "sdf_power";

typedef struct {
  SemaphoreHandle_t lock;
  TaskHandle_t task;
  bool initialized;
  sdf_power_manager_config_t config;
  // Wake configuration
  uint32_t enabled_sources;
  uint32_t finger_wake_debounce_ms;
  bool enable_ble_wake;
  bool enable_zigbee_wake;
  uint32_t deep_sleep_min_duration_ms;
  int64_t last_activity_us;
  int64_t wake_guard_until_us;
  int64_t next_battery_report_us;
  uint8_t battery_percent;
} sdf_power_state_t;

static sdf_power_state_t s_state = {0};

static bool sdf_power_gpio_valid(gpio_num_t gpio) {
  return gpio >= 0 && gpio < GPIO_NUM_MAX;
}

/* Convert a raw ESP sleep wakeup cause into sdf_power's own wake-reason
 * domain, via sdf_platform's classification. Not a plain enum cast: the two
 * enums' values don't line up for every case (in particular
 * SDF_PLATFORM_WAKE_REASON_OTHER == 4 has no matching SDF_POWER_WAKE_REASON_*
 * value), so translate explicitly. */
SDF_POWER_STATIC sdf_power_wake_reason_t
sdf_power_map_wakeup_reason(esp_sleep_wakeup_cause_t cause) {
  switch (sdf_platform_map_wakeup_reason(cause)) {
  case SDF_PLATFORM_WAKE_REASON_TIMER:
    return SDF_POWER_WAKE_REASON_TIMER;
  case SDF_PLATFORM_WAKE_REASON_GPIO:
    return SDF_POWER_WAKE_REASON_FINGERPRINT;
  case SDF_PLATFORM_WAKE_REASON_USB:
  case SDF_PLATFORM_WAKE_REASON_OTHER:
  default:
    return SDF_POWER_WAKE_REASON_OTHER;
  }
}

static const char *
sdf_power_wakeup_reason_name(sdf_power_wake_reason_t reason) {
  switch (reason) {
  case SDF_POWER_WAKE_REASON_TIMER:
    return "timer";
  case SDF_POWER_WAKE_REASON_FINGERPRINT:
    return "fingerprint";
  case SDF_POWER_WAKE_REASON_OTHER:
    return "other";
  case SDF_POWER_WAKE_REASON_NONE:
  default:
    return "none";
  }
}

static void sdf_power_push_battery_percent(uint8_t battery_percent) {
  if (!sdf_protocol_zigbee_is_enabled()) {
    return;
  }

  esp_err_t err = sdf_protocol_zigbee_update_battery_percent(battery_percent);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    ESP_LOGW(TAG, "Battery update failed: %s", esp_err_to_name(err));
  }

  // Emit battery event
  sdf_event_router_event_t evt = {
      .type = SDF_EVENT_ROUTER_POWER_BATTERY,
      .priority = SDF_EVENT_ROUTER_PRIO_LOW,
      .timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL),
      .payload.power.remaining_ms = battery_percent,
  };
  sdf_event_router_emit(&evt);
}

static bool sdf_power_busy_now(const sdf_power_manager_config_t *config) {
  if (config == NULL || config->busy_cb == NULL) {
    return false;
  }
  return config->busy_cb(config->busy_ctx);
}

static void sdf_power_notify_wakeup(const sdf_power_manager_config_t *config,
                                    sdf_power_wake_reason_t reason) {
  if (config == NULL || config->wake_cb == NULL) {
    return;
  }
  config->wake_cb(config->wake_ctx, reason);

  // Emit wake event
  sdf_event_router_event_t evt = {
      .type = SDF_EVENT_ROUTER_POWER_WAKE,
      .priority = SDF_EVENT_ROUTER_PRIO_NORMAL,
      .timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL),
      .payload.power.remaining_ms = 0,
  };
  sdf_event_router_emit(&evt);
}

static esp_err_t sdf_power_configure_fingerprint_wakeup(gpio_num_t wake_gpio) {
  if (!sdf_power_gpio_valid(wake_gpio)) {
    return ESP_OK;
  }

  return sdf_platform_sleep_enable_gpio_wakeup_light(wake_gpio, 1);
}

static esp_err_t sdf_power_enter_light_sleep(const sdf_power_manager_config_t *config) {
  if (config == NULL || !config->enable_light_sleep) {
    return ESP_OK;
  }

  sdf_event_router_event_t sleep_evt = {
      .type = SDF_EVENT_ROUTER_POWER_SLEEP,
      .priority = SDF_EVENT_ROUTER_PRIO_LOW,
      .timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL),
      .payload.power.remaining_ms = config->checkin_interval_ms,
  };
  sdf_event_router_emit(&sleep_evt);

  sdf_platform_sleep_disable_all_wakeup_sources();
  /* sdf_platform_sleep_enable_timer_wakeup() takes milliseconds and does its
   * own ms->us conversion internally - passing pre-multiplied microseconds
   * here made every light-sleep check-in ~1000x longer than configured. */
  sdf_platform_sleep_enable_timer_wakeup(config->checkin_interval_ms);

  if (sdf_power_gpio_valid(config->fingerprint_wake_gpio)) {
    esp_err_t wake_err =
        sdf_platform_sleep_enable_gpio_wakeup_light(config->fingerprint_wake_gpio, 1);
    if (wake_err != ESP_OK) {
      ESP_LOGW(TAG, "Failed to configure fingerprint wake GPIO: %s",
               esp_err_to_name(wake_err));
    }
  }

#ifndef CONFIG_IDF_TARGET_LINUX
  bool ble_was_gated = false;
  if (config->enable_ble_radio_gating && config->ble_transport != NULL) {
    sdf_nuki_ble_set_enabled(config->ble_transport, false);
    ble_was_gated = true;
  }
#endif

  esp_err_t sleep_err = sdf_platform_sleep_light();
  if (sleep_err != ESP_OK) {
    ESP_LOGW(TAG, "sdf_platform_sleep_light failed: %s",
             esp_err_to_name(sleep_err));
  }

#ifndef CONFIG_IDF_TARGET_LINUX
  if (ble_was_gated) {
    sdf_nuki_ble_set_enabled(config->ble_transport, true);
  }
#endif

  sdf_power_wake_reason_t reason =
      sdf_power_map_wakeup_reason(esp_sleep_get_wakeup_cause());
  ESP_LOGI(TAG, "Woke from light sleep (%s)",
           sdf_power_wakeup_reason_name(reason));
  sdf_power_notify_wakeup(config, reason);

  return sleep_err;
}

static void sdf_power_sleep_once(const sdf_power_manager_config_t *config) {
  if (config == NULL || !config->enable_light_sleep) {
    return;
  }

  sdf_power_enter_light_sleep(config);
}

static void sdf_power_task(void *arg) {
  (void)arg;

#ifndef CONFIG_IDF_TARGET_LINUX
  esp_task_wdt_add(NULL);
#endif

  while (true) {
#ifndef CONFIG_IDF_TARGET_LINUX
    esp_task_wdt_reset();
#endif
    bool initialized = false;
    int64_t now_us = esp_timer_get_time();
    int64_t wake_guard_until_us = 0;
    int64_t next_battery_report_us = 0;
    uint8_t battery_percent = 0;
    sdf_power_manager_config_t config_snapshot = {0};
    uint32_t enabled_sources_snapshot = 0;

    if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(SDF_POWER_LOCK_WAIT_MS)) ==
        pdTRUE) {
      initialized = s_state.initialized;
      config_snapshot = s_state.config;
      wake_guard_until_us = s_state.wake_guard_until_us;
      next_battery_report_us = s_state.next_battery_report_us;
      battery_percent = s_state.battery_percent;
      enabled_sources_snapshot = s_state.enabled_sources;
      xSemaphoreGive(s_state.lock);
    } else {
      vTaskDelay(pdMS_TO_TICKS(SDF_POWER_LOCK_WAIT_MS));
      continue;
    }

    if (!initialized) {
      vTaskDelay(pdMS_TO_TICKS(config_snapshot.loop_interval_ms));
      continue;
    }

    /* Evaluate policy decision. last_activity_us is sourced from the
     * policy module's own tracked state (sdf_power_policy_get_last_activity_us())
     * rather than a locally-cached snapshot, so that activity recorded via
     * sdf_power_policy_mark_activity() - called from sdf_power_mark_activity()
     * whenever real activity occurs - is actually visible here instead of
     * being silently discarded. Do NOT call sdf_power_policy_mark_activity()
     * unconditionally on every loop tick: that would stamp "now" as the last
     * activity time on every iteration and prevent idle timeout from ever
     * being reached, permanently disabling light/deep sleep. */
    sdf_power_policy_decision_t decision = sdf_power_policy_evaluate(
        now_us, sdf_power_policy_get_last_activity_us(), wake_guard_until_us,
        next_battery_report_us);

    if (now_us >= next_battery_report_us) {
      int battery_cb_result = -1;
      if (config_snapshot.battery_cb != NULL) {
        battery_cb_result = config_snapshot.battery_cb(config_snapshot.battery_ctx);
      }

      if (battery_cb_result >= 0 && battery_cb_result <= 100) {
        battery_percent = (uint8_t)battery_cb_result;
      }

      if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(SDF_POWER_LOCK_WAIT_MS)) ==
          pdTRUE) {
        s_state.battery_percent = battery_percent;
        s_state.next_battery_report_us =
            now_us +
            ((int64_t)config_snapshot.battery_report_interval_ms * 1000LL);
        s_state.wake_guard_until_us =
            esp_timer_get_time() +
            ((int64_t)config_snapshot.post_wake_guard_ms * 1000LL);
        xSemaphoreGive(s_state.lock);
      }
      sdf_power_push_battery_percent(battery_percent);
    }

#ifndef CONFIG_IDF_TARGET_LINUX
    if (decision == SDF_POWER_POLICY_DECISION_SLEEP_LIGHT || 
        decision == SDF_POWER_POLICY_DECISION_SLEEP_DEEP) {
      if (usb_serial_jtag_is_connected()) {
        decision = SDF_POWER_POLICY_DECISION_STAY_AWAKE;
      }
    }
#endif

    if (decision == SDF_POWER_POLICY_DECISION_SLEEP_LIGHT) {
      esp_err_t sleep_err = sdf_power_enter_light_sleep(&config_snapshot);
      if (sleep_err != ESP_OK) {
        ESP_LOGW(TAG, "Light sleep entry failed: %s",
                 esp_err_to_name(sleep_err));
      }
      sdf_power_policy_handle_wake(SDF_POWER_POLICY_WAKE_REASON_TIMER);
    }

    if (decision == SDF_POWER_POLICY_DECISION_SLEEP_DEEP) {
      sdf_platform_power_disable_all_wake();
      if (sdf_power_gpio_valid(config_snapshot.fingerprint_wake_gpio) &&
          (enabled_sources_snapshot & SDF_WAKE_SRC_FINGERPRINT_GPIO)) {
        sdf_platform_sleep_enable_gpio_wakeup_deep(config_snapshot.fingerprint_wake_gpio, 1);
      }
      if (enabled_sources_snapshot & SDF_WAKE_SRC_TIMER) {
        sdf_platform_sleep_enable_timer_wakeup(config_snapshot.checkin_interval_ms);
      }

      sdf_power_retention_t retention = {0};
      retention.last_activity_us = esp_timer_get_time();
      retention.next_checkin_us = retention.last_activity_us +
          ((int64_t)config_snapshot.checkin_interval_ms * 1000LL);
      sdf_power_save_retention(&retention);

      sdf_platform_power_enter_deep();
    }

    vTaskDelay(pdMS_TO_TICKS(config_snapshot.loop_interval_ms));
  }
}

void sdf_power_init(void) {
  const sdf_config_t *cfg = sdf_config_get();
  
  /* Initialize platform power first */
  sdf_platform_power_init();
  
  /* Initialize policy component */
  sdf_power_policy_config_t policy_cfg = {
      .checkin_interval_ms = cfg->checkin_interval_ms,
      .idle_before_sleep_ms = cfg->idle_before_sleep_ms,
      .post_wake_guard_ms = cfg->post_wake_guard_ms,
      .loop_interval_ms = cfg->power_loop_interval_ms,
      .battery_report_interval_ms = cfg->battery_report_interval_ms,
      .enable_light_sleep = cfg->enable_light_sleep,
      .enable_ble_radio_gating = cfg->enable_ble_radio_gating,
      .enable_deep_sleep_fallback = cfg->enable_deep_sleep_fallback,
      .fp_wake_gpio = cfg->fp_wake_gpio,
      .busy_cb = NULL,
      .wake_cb = NULL,
      .battery_cb = NULL,
      .zigbee_ready_cb = NULL,
      .ctx = NULL,
  };
  sdf_power_policy_init(&policy_cfg);
  
  sdf_power_manager_config_t config = {
      .ble_transport = NULL,
      .fingerprint_wake_gpio = (gpio_num_t)cfg->fp_wake_gpio,
      .checkin_interval_ms = cfg->checkin_interval_ms,
      .idle_before_sleep_ms = cfg->idle_before_sleep_ms,
      .post_wake_guard_ms = cfg->post_wake_guard_ms,
      .loop_interval_ms = cfg->power_loop_interval_ms,
      .battery_report_interval_ms = cfg->battery_report_interval_ms,
      .enable_light_sleep = cfg->enable_light_sleep,
      .enable_ble_radio_gating = cfg->enable_ble_radio_gating,
      .enable_deep_sleep_fallback = cfg->enable_deep_sleep_fallback,
      .battery_percent_default = cfg->battery_default_percent,
      .busy_cb = NULL,
      .busy_ctx = NULL,
      .wake_cb = NULL,
      .wake_ctx = NULL,
      .battery_cb = NULL,
      .battery_ctx = NULL,
  };
  sdf_power_init_power_manager(&config);
}



esp_err_t
sdf_power_init_power_manager(const sdf_power_manager_config_t *config) {
  if (config == NULL ||
      config->checkin_interval_ms < SDF_POWER_CHECKIN_INTERVAL_MIN_MS ||
      config->checkin_interval_ms > SDF_POWER_CHECKIN_INTERVAL_MAX_MS ||
      config->idle_before_sleep_ms == 0 || config->post_wake_guard_ms == 0 ||
      config->loop_interval_ms == 0 ||
      config->battery_report_interval_ms == 0 ||
      config->battery_percent_default > 100) {
    return ESP_ERR_INVALID_ARG;
  }

  if (s_state.lock == NULL) {
    s_state.lock = xSemaphoreCreateMutex();
    if (s_state.lock == NULL) {
      return ESP_ERR_NO_MEM;
    }
  }

  if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(SDF_POWER_LOCK_WAIT_MS)) !=
      pdTRUE) {
    return ESP_ERR_TIMEOUT;
  }

  if (s_state.initialized) {
    xSemaphoreGive(s_state.lock);
    return ESP_OK;
  }

  s_state.config = *config;
  s_state.battery_percent = config->battery_percent_default;
  s_state.enabled_sources = SDF_WAKE_SRC_TIMER | SDF_WAKE_SRC_FINGERPRINT_GPIO | SDF_WAKE_SRC_USB;
  s_state.finger_wake_debounce_ms = 100;
  s_state.enable_ble_wake = false;
  s_state.enable_zigbee_wake = false;
  s_state.deep_sleep_min_duration_ms = 30000;  // 30 seconds minimum
  s_state.last_activity_us = esp_timer_get_time();
  s_state.wake_guard_until_us =
      s_state.last_activity_us + ((int64_t)config->post_wake_guard_ms * 1000LL);
  s_state.next_battery_report_us = s_state.last_activity_us;
  xSemaphoreGive(s_state.lock);

  esp_err_t zigbee_cfg_err =
      sdf_protocol_zigbee_set_checkin_interval_ms(config->checkin_interval_ms);
  if (zigbee_cfg_err != ESP_OK && zigbee_cfg_err != ESP_ERR_INVALID_STATE) {
    return zigbee_cfg_err;
  }
  if (zigbee_cfg_err == ESP_ERR_INVALID_STATE) {
    ESP_LOGD(TAG, "Zigbee check-in update ignored (stack already running)");
  }

  BaseType_t task_ok =
      xTaskCreate(sdf_power_task, SDF_POWER_TASK_NAME, SDF_POWER_TASK_STACK,
                  NULL, SDF_POWER_TASK_PRIORITY, &s_state.task);
  if (task_ok != pdPASS) {
    return ESP_FAIL;
  }

  if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(SDF_POWER_LOCK_WAIT_MS)) ==
      pdTRUE) {
    s_state.initialized = true;
    xSemaphoreGive(s_state.lock);
  }

  sdf_power_push_battery_percent(config->battery_percent_default);

  ESP_LOGI(TAG,
           "Power manager started (checkin=%u ms, idle_sleep=%u ms, "
           "wake_gpio=%d, light_sleep=%u)",
           (unsigned)config->checkin_interval_ms,
           (unsigned)config->idle_before_sleep_ms,
           (int)config->fingerprint_wake_gpio,
           (unsigned)config->enable_light_sleep);
  return ESP_OK;
}

bool sdf_power_power_manager_ready(void) {
  if (s_state.lock == NULL) {
    return false;
  }

  bool ready = false;
  if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(SDF_POWER_LOCK_WAIT_MS)) ==
      pdTRUE) {
    ready = s_state.initialized;
    xSemaphoreGive(s_state.lock);
  }
  return ready;
}

void sdf_power_mark_activity(void) {
  if (s_state.lock == NULL) {
    return;
  }

  int64_t now_us = esp_timer_get_time();
  if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(SDF_POWER_LOCK_WAIT_MS)) ==
      pdTRUE) {
    s_state.last_activity_us = now_us;
    xSemaphoreGive(s_state.lock);
  }

  /* Keep the power policy module's own activity timestamp in sync so that
   * sdf_power_policy_evaluate() (which reads it via
   * sdf_power_policy_get_last_activity_us()) actually observes real
   * activity events instead of the call being a no-op. */
  sdf_power_policy_mark_activity();
}

esp_err_t sdf_power_set_checkin_interval_ms(uint32_t checkin_interval_ms) {
  if (checkin_interval_ms < SDF_POWER_CHECKIN_INTERVAL_MIN_MS ||
      checkin_interval_ms > SDF_POWER_CHECKIN_INTERVAL_MAX_MS) {
    return ESP_ERR_INVALID_ARG;
  }

  esp_err_t err =
      sdf_protocol_zigbee_set_checkin_interval_ms(checkin_interval_ms);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    return err;
  }

  if (s_state.lock != NULL &&
      xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(SDF_POWER_LOCK_WAIT_MS)) ==
          pdTRUE) {
    s_state.config.checkin_interval_ms = checkin_interval_ms;
    xSemaphoreGive(s_state.lock);
  }

  return err;
}

uint32_t sdf_power_get_checkin_interval_ms(void) {
  if (s_state.lock != NULL &&
      xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(SDF_POWER_LOCK_WAIT_MS)) ==
          pdTRUE) {
    uint32_t interval_ms = s_state.config.checkin_interval_ms;
    xSemaphoreGive(s_state.lock);
    return interval_ms;
  }

  return sdf_protocol_zigbee_get_checkin_interval_ms();
}

esp_err_t sdf_power_set_battery_percent(uint8_t battery_percent) {
  if (battery_percent > 100U) {
    return ESP_ERR_INVALID_ARG;
  }

  if (s_state.lock != NULL &&
      xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(SDF_POWER_LOCK_WAIT_MS)) ==
          pdTRUE) {
    s_state.battery_percent = battery_percent;
    xSemaphoreGive(s_state.lock);
  }

  sdf_power_push_battery_percent(battery_percent);
  return ESP_OK;
}

uint8_t sdf_power_get_battery_percent(void) {
  if (s_state.lock != NULL &&
      xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(SDF_POWER_LOCK_WAIT_MS)) ==
          pdTRUE) {
    uint8_t battery_percent = s_state.battery_percent;
    xSemaphoreGive(s_state.lock);
    return battery_percent;
  }

  return s_state.battery_percent;
}

esp_err_t sdf_power_set_wake_config(const sdf_power_wake_config_t *config) {
  if (config == NULL) {
    return ESP_ERR_INVALID_ARG;
  }
  if (config->checkin_interval_ms < SDF_POWER_CHECKIN_INTERVAL_MIN_MS ||
      config->checkin_interval_ms > SDF_POWER_CHECKIN_INTERVAL_MAX_MS) {
    return ESP_ERR_INVALID_ARG;
  }

  if (s_state.lock != NULL &&
      xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(SDF_POWER_LOCK_WAIT_MS)) ==
          pdTRUE) {
    s_state.enabled_sources = config->enabled_sources;
    s_state.finger_wake_debounce_ms = config->finger_wake_debounce_ms;
    s_state.enable_ble_wake = config->enable_ble_wake;
    s_state.enable_zigbee_wake = config->enable_zigbee_wake;
    s_state.deep_sleep_min_duration_ms = config->deep_sleep_min_duration_ms;
    xSemaphoreGive(s_state.lock);
  }

  return ESP_OK;
}

esp_err_t sdf_power_get_wake_config(sdf_power_wake_config_t *config) {
  if (config == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  if (s_state.lock != NULL &&
      xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(SDF_POWER_LOCK_WAIT_MS)) ==
          pdTRUE) {
    config->enabled_sources = s_state.enabled_sources;
    config->checkin_interval_ms = s_state.config.checkin_interval_ms;
    config->finger_wake_debounce_ms = s_state.finger_wake_debounce_ms;
    config->enable_ble_wake = s_state.enable_ble_wake;
    config->enable_zigbee_wake = s_state.enable_zigbee_wake;
    config->deep_sleep_min_duration_ms = s_state.deep_sleep_min_duration_ms;
    xSemaphoreGive(s_state.lock);
    return ESP_OK;
  }

  return ESP_FAIL;
}

static uint16_t sdf_power_crc16(const void *data, size_t len) {
  uint16_t crc = 0xFFFF;
  const uint8_t *p = (const uint8_t *)data;
  for (size_t i = 0; i < len; i++) {
    crc ^= p[i] << 8;
    for (int j = 0; j < 8; j++) {
      crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : (crc << 1);
    }
  }
  return crc;
}

esp_err_t sdf_power_save_retention(const sdf_power_retention_t *state) {
  if (state == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  sdf_power_retention_t save_state = *state;
  save_state.magic = 0x5FDEC3A1;
  save_state.crc16 = sdf_power_crc16(&save_state, sizeof(sdf_power_retention_t) - sizeof(save_state.crc16));

  return sdf_platform_sleep_retention_write(&save_state, sizeof(sdf_power_retention_t));
}

esp_err_t sdf_power_load_retention(sdf_power_retention_t *state) {
  if (state == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  esp_err_t err = sdf_platform_sleep_retention_read(state, sizeof(sdf_power_retention_t));
  if (err != ESP_OK) {
    return err;
  }

  uint16_t expected_crc = sdf_power_crc16(state, sizeof(sdf_power_retention_t) - sizeof(state->crc16));
  if (state->magic != 0x5FDEC3A1 || state->crc16 != expected_crc) {
    return ESP_ERR_INVALID_STATE;
  }

  return ESP_OK;
}

bool sdf_power_retention_valid(void) {
  return sdf_platform_sleep_retention_valid();
}

esp_err_t sdf_power_prepare_deep_sleep(sdf_power_retention_t *state) {
  if (state == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  // Populate retention state
  state->magic = 0x5FDEC3A1;
  state->last_activity_us = s_state.last_activity_us;
  state->next_checkin_us = esp_timer_get_time() + ((int64_t)s_state.config.checkin_interval_ms * 1000LL);
  state->ble_transport_state = (s_state.config.ble_transport != NULL && sdf_nuki_ble_is_enabled(s_state.config.ble_transport)) ? 1 : 0;
  state->zigbee_join_state = sdf_protocol_zigbee_is_ready() ? 1 : 0;
  state->sensor_power_state = 0;
  state->enrolled_user_count = 0;
  state->failed_attempts = 0;

  state->crc16 = sdf_power_crc16(state, sizeof(sdf_power_retention_t) - sizeof(state->crc16));

  return sdf_platform_sleep_retention_write(state, sizeof(sdf_power_retention_t));
}

esp_err_t sdf_power_resume_from_deep_sleep(sdf_wake_source_t *src) {
  if (src == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  // Determine wake source
  if (sdf_platform_sleep_wakeup_from_timer()) {
    *src = SDF_WAKE_SRC_TIMER;
  } else if (sdf_platform_sleep_wakeup_from_gpio(NULL)) {
    *src = SDF_WAKE_SRC_FINGERPRINT_GPIO;
  } else if (sdf_platform_sleep_wakeup_from_usb()) {
    *src = SDF_WAKE_SRC_USB;
  } else {
    *src = SDF_WAKE_SRC_TIMER;
  }

  // Emit wake event with source info
  sdf_event_router_event_t evt = {
      .type = SDF_EVENT_ROUTER_POWER_WAKE,
      .priority = SDF_EVENT_ROUTER_PRIO_NORMAL,
      .timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL),
      .payload.power.remaining_ms = 0,  // Would carry source info
  };
  sdf_event_router_emit(&evt);

  return ESP_OK;
}

uint32_t sdf_power_calculate_checkin_interval(void) {
  uint8_t battery = sdf_power_get_battery_percent();
  uint32_t base = s_state.config.checkin_interval_ms;

  if (battery < 20) return base * 4;      // 60s - critical
  if (battery < 40) return base * 2;      // 30s - low
  if (battery < 60) return (base * 3) / 2; // 22s - medium
  return base;                             // 15s - normal
}
