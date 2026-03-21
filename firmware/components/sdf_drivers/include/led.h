#ifndef LED_H
#define LED_H

#include "esp_err.h"

typedef struct {
  int gpio_num;
} sdf_led_config_t;

esp_err_t led_init(const sdf_led_config_t *config);
void led_deinit(void);

void led_off(void);
void led_breathe_white(void);
void led_pulse_blue(void);
void led_flash_green(void);
void led_solid_green(void);
void led_enrollment_step_green(void);
void led_enrollment_success_green(void);
void led_pulse_yellow(void);
void led_rapid_yellow(void);
void led_pulse_purple(void);
void led_rapid_purple(void);
void led_pulse_red(void);
void led_flash_red(void);
void led_flash_orange(void);

#endif /* LED_H */
