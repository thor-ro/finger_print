#ifndef SDF_MOCK_LINUX_SLEEP_H
#define SDF_MOCK_LINUX_SLEEP_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ESP_SLEEP_WAKEUP_ALL = 0,
    ESP_SLEEP_WAKEUP_TIMER = 1,
    ESP_SLEEP_WAKEUP_GPIO = 2,
    ESP_SLEEP_WAKEUP_USB = 3,
    ESP_SLEEP_WAKEUP_UNDEFINED = 0xFF,
} esp_sleep_wakeup_cause_t;

typedef enum {
    ESP_GPIO_WAKEUP_GPIO_LOW = 0,
    ESP_GPIO_WAKEUP_GPIO_HIGH = 1,
} esp_gpio_wakeup_level_t;

esp_err_t esp_sleep_enable_timer_wakeup_mock(uint64_t time_in_us);
esp_err_t esp_sleep_enable_gpio_wakeup_mock(void);
esp_err_t esp_sleep_disable_wakeup_source_mock(uint32_t source);
esp_err_t esp_light_sleep_start_mock(void);
void esp_deep_sleep_start_mock(void);
void esp_deep_sleep_enable_gpio_wakeup_mock(uint64_t gpio_mask, esp_gpio_wakeup_level_t level);

esp_sleep_wakeup_cause_t esp_sleep_get_wakeup_cause_mock(void);
uint64_t esp_sleep_get_gpio_wakeup_status_mock(void);

#define esp_sleep_enable_timer_wakeup esp_sleep_enable_timer_wakeup_mock
#define esp_sleep_enable_gpio_wakeup esp_sleep_enable_gpio_wakeup_mock
#define esp_sleep_disable_wakeup_source esp_sleep_disable_wakeup_source_mock
#define esp_light_sleep_start esp_light_sleep_start_mock
#define esp_deep_sleep_start esp_deep_sleep_start_mock
#define esp_deep_sleep_enable_gpio_wakeup esp_deep_sleep_enable_gpio_wakeup_mock
#define esp_sleep_get_wakeup_cause esp_sleep_get_wakeup_cause_mock
#define esp_sleep_get_gpio_wakeup_status esp_sleep_get_gpio_wakeup_status_mock

typedef enum {
    ESP_OK = 0,
    ESP_ERR_INVALID_ARG = -1,
    ESP_ERR_INVALID_STATE = -2,
    ESP_ERR_SLEEP_REJECTED = -3,
} esp_err_t;

typedef enum {
    GPIO_NUM_0 = 0,
    GPIO_NUM_1 = 1,
    GPIO_NUM_2 = 2,
    GPIO_NUM_3 = 3,
    GPIO_NUM_4 = 4,
    GPIO_NUM_5 = 5,
    GPIO_NUM_6 = 6,
    GPIO_NUM_7 = 7,
    GPIO_NUM_8 = 8,
    GPIO_NUM_9 = 9,
    GPIO_NUM_NC = -1,
} gpio_num_t;

typedef enum {
    GPIO_INTR_DISABLE = 0,
    GPIO_INTR_POSEDGE = 1,
    GPIO_INTR_NEGEDGE = 2,
    GPIO_INTR_ANYEDGE = 3,
    GPIO_INTR_LOW_LEVEL = 4,
    GPIO_INTR_HIGH_LEVEL = 5,
} gpio_int_type_t;

typedef enum {
    RTC_GPIO_MODE_INPUT_ONLY = 0,
    RTC_GPIO_MODE_OUTPUT_ONLY = 1,
    RTC_GPIO_MODE_INPUT_OUTPUT = 2,
} rtc_gpio_mode_t;

esp_err_t rtc_gpio_init_mock(gpio_num_t gpio_num);
esp_err_t rtc_gpio_set_direction_mock(gpio_num_t gpio_num, rtc_gpio_mode_t mode);
void rtc_gpio_pullup_en_mock(gpio_num_t gpio_num);
void rtc_gpio_pulldown_en_mock(gpio_num_t gpio_num);
void rtc_gpio_pullup_dis_mock(gpio_num_t gpio_num);
void rtc_gpio_pulldown_dis_mock(gpio_num_t gpio_num);

#define rtc_gpio_init rtc_gpio_init_mock
#define rtc_gpio_set_direction rtc_gpio_set_direction_mock
#define rtc_gpio_pullup_en rtc_gpio_pullup_en_mock
#define rtc_gpio_pulldown_en rtc_gpio_pulldown_en_mock
#define rtc_gpio_pullup_dis rtc_gpio_pullup_dis_mock
#define rtc_gpio_pulldown_dis rtc_gpio_pulldown_dis_mock

#define GPIO_MODE_INPUT 0
#define GPIO_MODE_OUTPUT 1

esp_err_t gpio_wakeup_enable_mock(gpio_num_t gpio_num, gpio_int_type_t intr_type);
esp_err_t gpio_wakeup_disable_mock(gpio_num_t gpio_num);

#define gpio_wakeup_enable gpio_wakeup_enable_mock
#define gpio_wakeup_disable gpio_wakeup_disable_mock

#ifdef __cplusplus
}
#endif

#endif /* SDF_MOCK_LINUX_SLEEP_H */