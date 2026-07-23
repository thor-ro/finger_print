#include "sdf_platform_sleep.h"

#ifndef CONFIG_IDF_TARGET_LINUX
#include "esp_sleep.h"
#include "driver/rtc_io.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"
#else
#include "sdf_mock_linux_sleep.h"
#endif

static const char *TAG = "sdf_platform_sleep";

sdf_platform_wake_reason_t sdf_platform_map_wakeup_reason(esp_sleep_wakeup_cause_t cause) {
    switch (cause) {
        case ESP_SLEEP_WAKEUP_TIMER:
            return SDF_PLATFORM_WAKE_REASON_TIMER;
        case ESP_SLEEP_WAKEUP_GPIO:
            return SDF_PLATFORM_WAKE_REASON_GPIO;
#if defined(ESP_SLEEP_WAKEUP_USB)
        case ESP_SLEEP_WAKEUP_USB:
            return SDF_PLATFORM_WAKE_REASON_USB;
#endif
        case ESP_SLEEP_WAKEUP_UNDEFINED:
        case ESP_SLEEP_WAKEUP_ALL:
        default:
            return SDF_PLATFORM_WAKE_REASON_OTHER;
    }
}

esp_err_t sdf_platform_sleep_enable_timer_wakeup(uint32_t interval_ms) {
    return esp_sleep_enable_timer_wakeup((uint64_t)interval_ms * 1000ULL);
}

esp_err_t sdf_platform_sleep_enable_gpio_wakeup_light(gpio_num_t gpio_num, int level) {
#ifndef CONFIG_IDF_TARGET_LINUX
    esp_err_t err = gpio_wakeup_enable(gpio_num, level ? GPIO_INTR_HIGH_LEVEL : GPIO_INTR_LOW_LEVEL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable GPIO %d wakeup: %s", gpio_num, esp_err_to_name(err));
        return err;
    }
    return esp_sleep_enable_gpio_wakeup();
#else
    return ESP_OK;
#endif
}

esp_err_t sdf_platform_sleep_enable_gpio_wakeup_deep(gpio_num_t gpio_num, int level) {
#ifndef CONFIG_IDF_TARGET_LINUX
    if (gpio_num >= GPIO_NUM_8) {
        ESP_LOGE(TAG, "GPIO %d not RTC-capable for deep sleep wakeup", gpio_num);
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = rtc_gpio_init(gpio_num);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init RTC GPIO %d: %s", gpio_num, esp_err_to_name(err));
        return err;
    }

    err = rtc_gpio_set_direction(gpio_num, RTC_GPIO_MODE_INPUT_ONLY);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set RTC GPIO direction: %s", esp_err_to_name(err));
        return err;
    }

    if (level) {
        rtc_gpio_pullup_en(gpio_num);
        rtc_gpio_pulldown_dis(gpio_num);
    } else {
        rtc_gpio_pullup_dis(gpio_num);
        rtc_gpio_pulldown_en(gpio_num);
    }

    return esp_deep_sleep_enable_gpio_wakeup(1ULL << gpio_num, level ? ESP_GPIO_WAKEUP_GPIO_HIGH : ESP_GPIO_WAKEUP_GPIO_LOW);
#else
    return ESP_OK;
#endif
}

esp_err_t sdf_platform_sleep_disable_all_wakeup_sources(void) {
    return esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
}

esp_err_t sdf_platform_sleep_light(void) {
    return esp_light_sleep_start();
}

esp_err_t sdf_platform_sleep_deep(void) {
    esp_deep_sleep_start();
    return ESP_OK;
}

uint64_t sdf_platform_sleep_get_sleep_duration_us(void) {
    // ESP-IDF doesn't directly provide sleep duration; return 0 as placeholder
    // For actual implementation, would need RTC memory to store sleep start time
    return 0;
}

bool sdf_platform_sleep_wakeup_from_gpio(gpio_num_t *gpio_num) {
#ifndef CONFIG_IDF_TARGET_LINUX
    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_GPIO) {
        uint64_t gpio_mask = esp_sleep_get_gpio_wakeup_status();
        if (gpio_mask != 0) {
            if (gpio_num != NULL) {
                *gpio_num = __builtin_ctzll(gpio_mask);
            }
            return true;
        }
    }
#endif
    return false;
}