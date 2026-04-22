#include "led.h"

#ifndef CONFIG_IDF_TARGET_LINUX

#include <stdbool.h>
#include <stdint.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "led_strip.h"

static const char *TAG = "led";

#define LED_TASK_STACK_SIZE 2048
#define LED_TASK_PRIORITY 4
#define LED_QUEUE_LENGTH 4

/**
 * LED command IDs – each maps to a specific animation.
 * LED_CMD_OFF is also used to preempt any running animation.
 */
typedef enum {
  LED_CMD_OFF = 0,
  LED_CMD_BREATHE_WHITE,
  LED_CMD_PULSE_BLUE,
  LED_CMD_FLASH_GREEN,
  LED_CMD_SOLID_GREEN,
  LED_CMD_ENROLLMENT_STEP_GREEN,
  LED_CMD_ENROLLMENT_SUCCESS_GREEN,
  LED_CMD_PULSE_YELLOW,
  LED_CMD_RAPID_YELLOW,
  LED_CMD_PULSE_PURPLE,
  LED_CMD_RAPID_PURPLE,
  LED_CMD_PULSE_RED,
  LED_CMD_FLASH_RED,
  LED_CMD_FLASH_ORANGE,
} led_cmd_t;

typedef struct {
  bool initialized;
  int gpio_num;
  led_strip_handle_t strip;
  TaskHandle_t task;
  QueueHandle_t queue;
} led_state_t;

static led_state_t s_state = {
    .initialized = false,
    .gpio_num = -1,
    .strip = NULL,
    .task = NULL,
    .queue = NULL,
};

static void led_set_rgb(uint8_t red, uint8_t green, uint8_t blue) {
  if (!s_state.initialized || s_state.strip == NULL) {
    return;
  }

  led_strip_set_pixel(s_state.strip, 0, red, green, blue);
  led_strip_refresh(s_state.strip);
}

static void led_clear(void) {
  if (!s_state.initialized || s_state.strip == NULL) {
    return;
  }

  led_strip_clear(s_state.strip);
}

/**
 * @brief Sleep in small increments, checking for a new command after each tick.
 * @return true if a new command arrived (animation should be aborted),
 *         false if the full delay elapsed without interruption.
 */
static bool led_interruptible_delay(uint32_t ms) {
  const uint32_t slice_ms = 50;
  uint32_t remaining = ms;
  led_cmd_t peek;

  while (remaining > 0) {
    uint32_t step = (remaining < slice_ms) ? remaining : slice_ms;
    vTaskDelay(pdMS_TO_TICKS(step));
    remaining -= step;

    if (xQueuePeek(s_state.queue, &peek, 0) == pdTRUE) {
      return true; /* New command waiting – abort current animation */
    }
  }
  return false;
}

/**
 * @brief Play a blink pattern, aborting early if a new command is queued.
 */
static void led_play_pattern(uint8_t red, uint8_t green, uint8_t blue,
                             uint32_t on_ms, uint32_t off_ms,
                             uint8_t cycles) {
  for (uint8_t i = 0; i < cycles; ++i) {
    led_set_rgb(red, green, blue);
    if (led_interruptible_delay(on_ms)) {
      led_clear();
      return;
    }
    led_clear();
    if (off_ms > 0 && led_interruptible_delay(off_ms)) {
      return;
    }
  }
}

/**
 * @brief Hold a solid colour, aborting early if a new command is queued.
 */
static void led_hold_color(uint8_t red, uint8_t green, uint8_t blue,
                           uint32_t hold_ms) {
  led_set_rgb(red, green, blue);
  led_interruptible_delay(hold_ms);
  led_clear();
}

static void led_execute_cmd(led_cmd_t cmd) {
  switch (cmd) {
  case LED_CMD_OFF:
    led_clear();
    break;
  case LED_CMD_BREATHE_WHITE:
    led_play_pattern(255, 255, 255, 500, 500, 3);
    break;
  case LED_CMD_PULSE_BLUE:
    led_play_pattern(0, 0, 255, 350, 250, 3);
    break;
  case LED_CMD_FLASH_GREEN:
    led_play_pattern(0, 255, 0, 200, 150, 1);
    break;
  case LED_CMD_SOLID_GREEN:
    led_hold_color(0, 255, 0, 2000);
    break;
  case LED_CMD_ENROLLMENT_STEP_GREEN:
    led_hold_color(0, 255, 0, 1000);
    break;
  case LED_CMD_ENROLLMENT_SUCCESS_GREEN:
    led_play_pattern(0, 255, 0, 1000, 1000, 3);
    break;
  case LED_CMD_PULSE_YELLOW:
    led_play_pattern(255, 180, 0, 350, 250, 3);
    break;
  case LED_CMD_RAPID_YELLOW:
    led_play_pattern(255, 180, 0, 150, 100, 6);
    break;
  case LED_CMD_PULSE_PURPLE:
    led_play_pattern(255, 0, 255, 350, 250, 3);
    break;
  case LED_CMD_RAPID_PURPLE:
    led_play_pattern(255, 0, 255, 150, 100, 6);
    break;
  case LED_CMD_PULSE_RED:
    led_play_pattern(255, 0, 0, 350, 250, 3);
    break;
  case LED_CMD_FLASH_RED:
    led_play_pattern(255, 0, 0, 250, 250, 3);
    break;
  case LED_CMD_FLASH_ORANGE:
    led_play_pattern(255, 165, 0, 250, 250, 5);
    break;
  }
}

static void led_task(void *arg) {
  (void)arg;
  led_cmd_t cmd;

  while (true) {
    if (xQueueReceive(s_state.queue, &cmd, portMAX_DELAY) == pdTRUE) {
      led_execute_cmd(cmd);
    }
  }
}

static void led_post_cmd(led_cmd_t cmd) {
  if (s_state.queue == NULL) {
    return;
  }

  /* Overwrite any pending commands – only the latest intent matters. */
  xQueueReset(s_state.queue);
  xQueueSend(s_state.queue, &cmd, 0);
}

esp_err_t led_init(const sdf_led_config_t *config) {
  if (config == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  if (s_state.initialized) {
    return ESP_OK;
  }

  s_state.gpio_num = config->gpio_num;
  if (config->gpio_num < 0) {
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

  esp_err_t err =
      led_strip_new_rmt_device(&led_config, &rmt_config, &s_state.strip);
  if (err != ESP_OK) {
    s_state.gpio_num = -1;
    return err;
  }

  s_state.queue = xQueueCreate(LED_QUEUE_LENGTH, sizeof(led_cmd_t));
  if (s_state.queue == NULL) {
    led_strip_del(s_state.strip);
    s_state.strip = NULL;
    s_state.gpio_num = -1;
    return ESP_ERR_NO_MEM;
  }

  BaseType_t rc = xTaskCreate(led_task, "led", LED_TASK_STACK_SIZE, NULL,
                              LED_TASK_PRIORITY, &s_state.task);
  if (rc != pdPASS) {
    vQueueDelete(s_state.queue);
    s_state.queue = NULL;
    led_strip_del(s_state.strip);
    s_state.strip = NULL;
    s_state.gpio_num = -1;
    return ESP_ERR_NO_MEM;
  }

  s_state.initialized = true;
  led_strip_clear(s_state.strip);

  ESP_LOGI(TAG, "LED strip initialized on GPIO %d (async task)", config->gpio_num);
  return ESP_OK;
}

void led_deinit(void) {
  if (!s_state.initialized) {
    return;
  }

  if (s_state.task != NULL) {
    vTaskDelete(s_state.task);
    s_state.task = NULL;
  }

  if (s_state.queue != NULL) {
    vQueueDelete(s_state.queue);
    s_state.queue = NULL;
  }

  if (s_state.strip != NULL) {
    led_strip_clear(s_state.strip);
    led_strip_del(s_state.strip);
    s_state.strip = NULL;
  }

  s_state.initialized = false;
  s_state.gpio_num = -1;
}

/* ---------- Non-blocking public API ---------- */

void led_off(void) { led_post_cmd(LED_CMD_OFF); }

void led_breathe_white(void) { led_post_cmd(LED_CMD_BREATHE_WHITE); }

void led_pulse_blue(void) { led_post_cmd(LED_CMD_PULSE_BLUE); }

void led_flash_green(void) { led_post_cmd(LED_CMD_FLASH_GREEN); }

void led_solid_green(void) { led_post_cmd(LED_CMD_SOLID_GREEN); }

void led_enrollment_step_green(void) {
  led_post_cmd(LED_CMD_ENROLLMENT_STEP_GREEN);
}

void led_enrollment_success_green(void) {
  led_post_cmd(LED_CMD_ENROLLMENT_SUCCESS_GREEN);
}

void led_pulse_yellow(void) { led_post_cmd(LED_CMD_PULSE_YELLOW); }

void led_rapid_yellow(void) { led_post_cmd(LED_CMD_RAPID_YELLOW); }

void led_pulse_purple(void) { led_post_cmd(LED_CMD_PULSE_PURPLE); }

void led_rapid_purple(void) { led_post_cmd(LED_CMD_RAPID_PURPLE); }

void led_pulse_red(void) { led_post_cmd(LED_CMD_PULSE_RED); }

void led_flash_red(void) { led_post_cmd(LED_CMD_FLASH_RED); }

void led_flash_orange(void) { led_post_cmd(LED_CMD_FLASH_ORANGE); }

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
