/**
 * @file sdf_mock_linux_services.h
 * @brief Mock types and defines for GPIO and LED strip on Linux host.
 *
 * Included by sdf_services.c in place of driver/gpio.h and led_strip.h.
 */
#ifndef SDF_MOCK_LINUX_SERVICES_H
#define SDF_MOCK_LINUX_SERVICES_H

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* --------------- GPIO types & defines --------------- */

typedef struct {
  uint64_t pin_bit_mask;
  int mode;
  int pull_up_en;
  int pull_down_en;
  int intr_type;
} gpio_config_t;

#define GPIO_MODE_INPUT 0
#define GPIO_MODE_OUTPUT 1
#define GPIO_PULLUP_DISABLE 0
#define GPIO_PULLUP_ENABLE 1
#define GPIO_PULLDOWN_DISABLE 0
#define GPIO_INTR_DISABLE 0
#define GPIO_INTR_ANYEDGE 3

typedef void (*gpio_isr_t)(void *);

esp_err_t gpio_config_mock(const gpio_config_t *config);
esp_err_t gpio_install_isr_service(int flags);
esp_err_t gpio_isr_handler_add(int gpio, gpio_isr_t isr, void *args);
int gpio_get_level(int gpio);
esp_err_t gpio_set_level(int gpio, int level);

#define gpio_config gpio_config_mock

/* --------------- LED strip types & defines --------------- */

typedef void *led_strip_handle_t;
#define LED_PIXEL_FORMAT_GRB 0
#define LED_MODEL_WS2812 0

typedef struct {
  int strip_gpio_num;
  int max_leds;
  int led_pixel_format;
  int led_model;
  struct {
    bool invert_out;
  } flags;
} led_strip_config_t;

typedef struct {
  int resolution_hz;
  struct {
    bool with_dma;
  } flags;
} led_strip_rmt_config_t;

esp_err_t led_strip_new_rmt_device(const led_strip_config_t *led_config,
                                   const led_strip_rmt_config_t *rmt_config,
                                   led_strip_handle_t *ret_strip);
esp_err_t led_strip_set_pixel(led_strip_handle_t strip, uint32_t index,
                              uint32_t red, uint32_t green, uint32_t blue);
esp_err_t led_strip_refresh(led_strip_handle_t strip);
esp_err_t led_strip_clear(led_strip_handle_t strip);

#endif /* SDF_MOCK_LINUX_SERVICES_H */
