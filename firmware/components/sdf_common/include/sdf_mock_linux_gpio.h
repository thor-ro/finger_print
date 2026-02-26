/**
 * @file sdf_mock_linux_gpio.h
 * @brief Shared GPIO mock types and defines for Linux host builds.
 *
 * This header consolidates duplicated GPIO type definitions that were
 * previously scattered across sdf_mock_linux_drivers.h and
 * sdf_mock_linux_services.h.  Both headers now include this file
 * instead of defining their own copies.
 */
#ifndef SDF_MOCK_LINUX_GPIO_H
#define SDF_MOCK_LINUX_GPIO_H

#ifdef CONFIG_IDF_TARGET_LINUX

#include "esp_err.h"
#include <stdint.h>

#include "hal/gpio_types.h"

typedef struct {
  uint64_t pin_bit_mask;
  int mode;
  int pull_up_en;
  int pull_down_en;
  int intr_type;
} gpio_config_t;

typedef void (*gpio_isr_t)(void *);

/* --------------- GPIO defines --------------- */

#define GPIO_MODE_INPUT 0
#define GPIO_MODE_OUTPUT 1
#define GPIO_PULLUP_DISABLE 0
#define GPIO_PULLUP_ENABLE 1
#define GPIO_PULLDOWN_DISABLE 0
#define GPIO_INTR_DISABLE 0
#define GPIO_INTR_ANYEDGE 3

/* --------------- GPIO mock functions --------------- */

esp_err_t gpio_config_mock(const gpio_config_t *config);
esp_err_t gpio_install_isr_service(int flags);
esp_err_t gpio_isr_handler_add(int gpio, gpio_isr_t isr, void *args);
int gpio_get_level(int gpio);
esp_err_t gpio_set_level(gpio_num_t gpio_num, uint32_t level);

/* Redirect gpio_config calls to mock to avoid name collision with struct */
#define gpio_config gpio_config_mock

#endif /* CONFIG_IDF_TARGET_LINUX */
#endif /* SDF_MOCK_LINUX_GPIO_H */
