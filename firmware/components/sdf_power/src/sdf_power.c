#include "sdf_power.h"

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

#ifdef SDF_POWER_TESTING
#define SDF_POWER_STATIC
#else
#define SDF_POWER_STATIC static
#endif

#define SDF_POWER_TASK_NAME "sdf_power"
#define SDF_POWER_TASK_STACK 4096
#define SDF_POWER_TASK_PRIORITY 4
#define SDF_POWER_LOCK_WAIT_MS 250u

#define SDF_POWER_CHECKIN_INTERVAL_DEFAULT_MS                                  \
  CONFIG_SDF_POWER_CHECKIN_INTERVAL_MS
#define SDF_POWER_CHECKIN_INTERVAL_MIN_MS 1000u
#define SDF_POWER_CHECKIN_INTERVAL_MAX_MS 600000u
#define SDF_POWER_IDLE_BEFORE_SLEEP_DEFAULT_MS                                 \
  CONFIG_SDF_POWER_IDLE_BEFORE_SLEEP_MS
#define SDF_POWER_POST_WAKE_GUARD_DEFAULT_MS CONFIG_SDF_POWER_POST_WAKE_GUARD_MS
#define SDF_POWER_LOOP_INTERVAL_DEFAULT_MS CONFIG_SDF_POWER_LOOP_INTERVAL_MS
#define SDF_POWER_BATTERY_REPORT_DEFAULT_MS                                    \
  CONFIG_SDF_POWER_BATTERY_REPORT_INTERVAL_MS
#define SDF_POWER_BATTERY_DEFAULT_PERCENT                                      \
  CONFIG_SDF_POWER_BATTERY_DEFAULT_PERCENT

static const char *TAG = "sdf_power";

typedef struct {
  SemaphoreHandle_t lock;
  TaskHandle_t task;
  bool initialized;
  sdf_power_manager_config_t config;
  int64_t last_activity_us;
  int64_t wake_guard_until_us;
  int64_t next_battery_report_us;
  uint8_t battery_percent;
} sdf_power_state_t;

static sdf_power_state_t s_state = {0};

static bool sdf_power_gpio_valid(gpio_num_t gpio) {
  return gpio >= 0 && gpio < GPIO_NUM_MAX;
}

SDF_POWER_STATIC sdf_power_wake_reason_t
sdf_power_map_wakeup_reason(esp_sleep_wakeup_cause_t cause) {
  switch (cause) {
  case ESP_SLEEP_WAKEUP_TIMER:
    return SDF_POWER_WAKE_REASON_TIMER;
  case ESP_SLEEP_WAKEUP_GPIO:
    return SDF_POWER_WAKE_REASON_FINGERPRINT;
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
  esp_err_t err = sdf_protocol_zigbee_update_battery_percent(battery_percent);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    ESP_LOGW(TAG, "Battery update failed: %s", esp_err_to_name(err));
  }
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
}

static esp_err_t sdf_power_configure_fingerprint_wakeup(gpio_num_t wake_gpio) {
  if (!sdf_power_gpio_valid(wake_gpio)) {
    return ESP_OK;
  }

#ifndef CONFIG_IDF_TARGET_LINUX
  esp_err_t err = gpio_wakeup_enable(wake_gpio, GPIO_INTR_HIGH_LEVEL);
  if (err != ESP_OK) {
    return err;
  }

  return esp_sleep_enable_gpio_wakeup();
#else
  return ESP_OK;
#endif
}

static void sdf_power_sleep_once(const sdf_power_manager_config_t *config) {
  if (config == NULL || !config->enable_light_sleep) {
    return;
  }

  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  esp_sleep_enable_timer_wakeup((uint64_t)config->checkin_interval_ms *
                                1000ULL);

  if (sdf_power_gpio_valid(config->fingerprint_wake_gpio)) {
    esp_err_t wake_err =
        sdf_power_configure_fingerprint_wakeup(config->fingerprint_wake_gpio);
    if (wake_err != ESP_OK) {
      ESP_LOGW(TAG, "Failed to configure fingerprint wake GPIO: %s",
               esp_err_to_name(wake_err));
    }
  }

#ifndef CONFIG_IDF_TARGET_LINUX
  if (config->enable_ble_radio_gating && config->ble_transport != NULL) {
    sdf_nuki_ble_set_enabled(config->ble_transport, false);
  }
#endif

  esp_err_t sleep_err = esp_light_sleep_start();
  if (sleep_err != ESP_OK) {
    ESP_LOGW(TAG, "esp_light_sleep_start failed: %s",
             esp_err_to_name(sleep_err));
  }

#ifndef CONFIG_IDF_TARGET_LINUX
  if (config->enable_ble_radio_gating && config->ble_transport != NULL) {
    sdf_nuki_ble_set_enabled(config->ble_transport, true);
  }
#endif

  sdf_power_wake_reason_t reason =
      sdf_power_map_wakeup_reason(esp_sleep_get_wakeup_cause());
  ESP_LOGI(TAG, "Woke from light sleep (%s)",
           sdf_power_wakeup_reason_name(reason));
  sdf_power_notify_wakeup(config, reason);
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
    sdf_power_manager_config_t config_snapshot;
    bool initialized = false;
    int64_t now_us = esp_timer_get_time();
    int64_t last_activity_us = now_us;
    int64_t wake_guard_until_us = now_us;
    int64_t next_battery_report_us = now_us;
    uint8_t battery_percent = SDF_POWER_BATTERY_DEFAULT_PERCENT;

    if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(SDF_POWER_LOCK_WAIT_MS)) ==
        pdTRUE) {
      initialized = s_state.initialized;
      config_snapshot = s_state.config;
      last_activity_us = s_state.last_activity_us;
      wake_guard_until_us = s_state.wake_guard_until_us;
      next_battery_report_us = s_state.next_battery_report_us;
      battery_percent = s_state.battery_percent;
      xSemaphoreGive(s_state.lock);
    } else {
      vTaskDelay(pdMS_TO_TICKS(SDF_POWER_LOOP_INTERVAL_DEFAULT_MS));
      continue;
    }

    if (!initialized) {
      vTaskDelay(pdMS_TO_TICKS(SDF_POWER_LOOP_INTERVAL_DEFAULT_MS));
      continue;
    }

    if (now_us >= next_battery_report_us) {
      int battery_cb_result = -1;
      if (config_snapshot.battery_cb != NULL) {
        battery_cb_result =
            config_snapshot.battery_cb(config_snapshot.battery_ctx);
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
        xSemaphoreGive(s_state.lock);
      }
      sdf_power_push_battery_percent(battery_percent);
    }

    bool allow_sleep = config_snapshot.enable_light_sleep;
    if (allow_sleep && now_us < wake_guard_until_us) {
      allow_sleep = false;
    }
    if (allow_sleep &&
        (now_us - last_activity_us) <
            ((int64_t)config_snapshot.idle_before_sleep_ms * 1000LL)) {
      allow_sleep = false;
    }
    if (allow_sleep && sdf_power_busy_now(&config_snapshot)) {
      allow_sleep = false;
    }

#ifndef CONFIG_IDF_TARGET_LINUX
    if (allow_sleep && usb_serial_jtag_is_connected()) {
      allow_sleep = false;
    }
#endif

    if (allow_sleep) {
      if (config_snapshot.enable_deep_sleep_fallback &&
          !sdf_protocol_zigbee_is_ready()) {
        ESP_LOGI(TAG, "Entering deep sleep (Zigbee not joined)");
        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);

        if (sdf_power_gpio_valid(config_snapshot.fingerprint_wake_gpio)) {
#ifndef CONFIG_IDF_TARGET_LINUX
          esp_deep_sleep_enable_gpio_wakeup(
              1ULL << config_snapshot.fingerprint_wake_gpio,
              ESP_GPIO_WAKEUP_GPIO_HIGH);
#endif
        }
        esp_deep_sleep_start();
      }

      sdf_power_sleep_once(&config_snapshot);
      int64_t wake_us = esp_timer_get_time();
      if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(SDF_POWER_LOCK_WAIT_MS)) ==
          pdTRUE) {
        s_state.last_activity_us = wake_us;
        s_state.wake_guard_until_us =
            wake_us + ((int64_t)config_snapshot.post_wake_guard_ms * 1000LL);
        xSemaphoreGive(s_state.lock);
      }
    }

    vTaskDelay(pdMS_TO_TICKS(config_snapshot.loop_interval_ms));
  }
}

void sdf_power_init(void) {
  sdf_power_manager_config_t config;
  sdf_power_get_default_power_config(&config);
  sdf_power_init_power_manager(&config);
}

void sdf_power_get_default_power_config(sdf_power_manager_config_t *config) {
  if (config == NULL) {
    return;
  }

  memset(config, 0, sizeof(*config));
  config->ble_transport = NULL;
  config->fingerprint_wake_gpio = (gpio_num_t)CONFIG_SDF_POWER_FP_WAKE_GPIO;
  config->checkin_interval_ms = SDF_POWER_CHECKIN_INTERVAL_DEFAULT_MS;
  config->idle_before_sleep_ms = SDF_POWER_IDLE_BEFORE_SLEEP_DEFAULT_MS;
  config->post_wake_guard_ms = SDF_POWER_POST_WAKE_GUARD_DEFAULT_MS;
  config->loop_interval_ms = SDF_POWER_LOOP_INTERVAL_DEFAULT_MS;
  config->battery_report_interval_ms = SDF_POWER_BATTERY_REPORT_DEFAULT_MS;
  config->enable_light_sleep = CONFIG_SDF_POWER_ENABLE_LIGHT_SLEEP;
  config->enable_ble_radio_gating = CONFIG_SDF_POWER_ENABLE_BLE_RADIO_GATING;
  config->enable_deep_sleep_fallback = false;
  config->battery_percent_default = SDF_POWER_BATTERY_DEFAULT_PERCENT;
  config->busy_cb = NULL;
  config->busy_ctx = NULL;
  config->wake_cb = NULL;
  config->wake_ctx = NULL;
  config->battery_cb = NULL;
  config->battery_ctx = NULL;
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

  return SDF_POWER_BATTERY_DEFAULT_PERCENT;
}
