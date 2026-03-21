#include "led.h"

#ifndef CONFIG_IDF_TARGET_LINUX

#include <stdbool.h>
#include <stdint.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "led_strip.h"

static const char *TAG = "led";

typedef struct {
  bool initialized;
  int gpio_num;
  led_strip_handle_t strip;
  SemaphoreHandle_t lock;
} led_state_t;

static led_state_t s_state = {
    .initialized = false,
    .gpio_num = -1,
    .strip = NULL,
    .lock = NULL,
};

static esp_err_t led_ensure_lock(void) {
  if (s_state.lock != NULL) {
    return ESP_OK;
  }

  s_state.lock = xSemaphoreCreateMutex();
  if (s_state.lock == NULL) {
    return ESP_ERR_NO_MEM;
  }

  return ESP_OK;
}

static void led_set_rgb_locked(uint8_t red, uint8_t green, uint8_t blue) {
  if (!s_state.initialized || s_state.strip == NULL) {
    return;
  }

  led_strip_set_pixel(s_state.strip, 0, red, green, blue);
  led_strip_refresh(s_state.strip);
}

static void led_clear_locked(void) {
  if (!s_state.initialized || s_state.strip == NULL) {
    return;
  }

  led_strip_clear(s_state.strip);
}

static void led_play_pattern(uint8_t red, uint8_t green, uint8_t blue,
                             uint32_t on_ms, uint32_t off_ms,
                             uint8_t cycles) {
  if (s_state.lock == NULL) {
    return;
  }

  if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(250)) != pdTRUE) {
    return;
  }

  if (!s_state.initialized || s_state.strip == NULL) {
    xSemaphoreGive(s_state.lock);
    return;
  }

  for (uint8_t i = 0; i < cycles; ++i) {
    led_set_rgb_locked(red, green, blue);
    vTaskDelay(pdMS_TO_TICKS(on_ms));
    led_clear_locked();
    if (off_ms > 0) {
      vTaskDelay(pdMS_TO_TICKS(off_ms));
    }
  }

  xSemaphoreGive(s_state.lock);
}

static void led_hold_color(uint8_t red, uint8_t green, uint8_t blue,
                           uint32_t hold_ms) {
  if (s_state.lock == NULL) {
    return;
  }

  if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(250)) != pdTRUE) {
    return;
  }

  if (!s_state.initialized || s_state.strip == NULL) {
    xSemaphoreGive(s_state.lock);
    return;
  }

  led_set_rgb_locked(red, green, blue);
  vTaskDelay(pdMS_TO_TICKS(hold_ms));
  led_clear_locked();
  xSemaphoreGive(s_state.lock);
}

esp_err_t led_init(const sdf_led_config_t *config) {
  if (config == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  esp_err_t err = led_ensure_lock();
  if (err != ESP_OK) {
    return err;
  }

  if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(250)) != pdTRUE) {
    return ESP_ERR_TIMEOUT;
  }

  if (s_state.initialized) {
    xSemaphoreGive(s_state.lock);
    return ESP_OK;
  }

  s_state.gpio_num = config->gpio_num;
  if (config->gpio_num < 0) {
    xSemaphoreGive(s_state.lock);
    return ESP_OK;
  }

  led_strip_config_t led_config = {
      .strip_gpio_num = config->gpio_num,
      .max_leds = 1,
      .led_model = LED_MODEL_WS2812,
      .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
      .flags.invert_out = false,
  };
  led_strip_rmt_config_t rmt_config = {
      .resolution_hz = 10 * 1000 * 1000,
      .flags.with_dma = false,
  };

  err = led_strip_new_rmt_device(&led_config, &rmt_config, &s_state.strip);
  if (err != ESP_OK) {
    s_state.gpio_num = -1;
    xSemaphoreGive(s_state.lock);
    return err;
  }

  s_state.initialized = true;
  led_clear_locked();
  xSemaphoreGive(s_state.lock);

  ESP_LOGI(TAG, "LED strip initialized on GPIO %d", config->gpio_num);
  return ESP_OK;
}

void led_deinit(void) {
  if (s_state.lock == NULL) {
    return;
  }

  if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(250)) != pdTRUE) {
    return;
  }

  if (s_state.initialized && s_state.strip != NULL) {
    led_clear_locked();
    led_strip_del(s_state.strip);
  }

  s_state.initialized = false;
  s_state.gpio_num = -1;
  s_state.strip = NULL;
  xSemaphoreGive(s_state.lock);
}

void led_off(void) {
  if (s_state.lock == NULL) {
    return;
  }

  if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(250)) != pdTRUE) {
    return;
  }

  led_clear_locked();
  xSemaphoreGive(s_state.lock);
}

void led_breathe_white(void) { led_play_pattern(255, 255, 255, 500, 500, 3); }

void led_pulse_blue(void) { led_play_pattern(0, 0, 255, 350, 250, 3); }

void led_flash_green(void) { led_play_pattern(0, 255, 0, 200, 150, 1); }

void led_solid_green(void) { led_hold_color(0, 255, 0, 2000); }

void led_enrollment_step_green(void) { led_hold_color(0, 255, 0, 1000); }

void led_enrollment_success_green(void) {
  led_play_pattern(0, 255, 0, 1000, 1000, 3);
}

void led_pulse_yellow(void) { led_play_pattern(255, 180, 0, 350, 250, 3); }

void led_rapid_yellow(void) {
  led_play_pattern(255, 180, 0, 150, 100, 6);
}

void led_pulse_purple(void) { led_play_pattern(255, 0, 255, 350, 250, 3); }

void led_rapid_purple(void) {
  led_play_pattern(255, 0, 255, 150, 100, 6);
}

void led_pulse_red(void) { led_play_pattern(255, 0, 0, 350, 250, 3); }

void led_flash_red(void) { led_play_pattern(255, 0, 0, 250, 250, 3); }

void led_flash_orange(void) { led_play_pattern(255, 165, 0, 250, 250, 5); }

#else

esp_err_t led_init(const sdf_led_config_t *config) {
  (void)config;
  return ESP_OK;
}

void led_deinit(void) {}

void led_off(void) {}
void led_breathe_white(void) {}
void led_pulse_blue(void) {}
void led_flash_green(void) {}
void led_solid_green(void) {}
void led_enrollment_step_green(void) {}
void led_enrollment_success_green(void) {}
void led_pulse_yellow(void) {}
void led_rapid_yellow(void) {}
void led_pulse_purple(void) {}
void led_rapid_purple(void) {}
void led_pulse_red(void) {}
void led_flash_red(void) {}
void led_flash_orange(void) {}

#endif
