#include "sdf_platform_sleep.h"
#include "sdf_config.h"

#ifndef CONFIG_IDF_TARGET_LINUX
#include "esp_sleep.h"
#include "driver/rtc_io.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"
#else
#include "sdf_mock_linux_sleep.h"
#endif

#ifndef CONFIG_IDF_TARGET_LINUX
#include "soc/rtc.h"
#endif

static uint8_t *s_retention_base = NULL;

static const char *TAG = "sdf_platform_sleep";

#define SDF_POWER_RETENTION_MAGIC 0x5FDEC3A1
#define SDF_POWER_DEFAULT_CHECKIN_INTERVAL_MS 15000

esp_err_t sdf_power_crc16_ccitt(const void *data, size_t len, uint16_t *crc) {
    uint16_t c = 0xFFFF;
    const uint8_t *p = (const uint8_t *)data;
    for (size_t i = 0; i < len; i++) {
        c ^= p[i] << 8;
        for (int j = 0; j < 8; j++) {
            c = (c & 0x8000) ? (c << 1) ^ 0x1021 : (c << 1);
        }
    }
    *crc = c;
    return ESP_OK;
}

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

bool sdf_platform_sleep_wakeup_from_timer(void) {
#ifndef CONFIG_IDF_TARGET_LINUX
    return esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER;
#else
    return false;
#endif
}

bool sdf_platform_sleep_wakeup_from_usb(void) {
#if defined(CONFIG_IDF_TARGET_LINUX)
    return false;
#elif defined(ESP_SLEEP_WAKEUP_USB)
    return esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_USB;
#else
    return false;
#endif
}

esp_err_t sdf_platform_sleep_configure_wake_sources(uint32_t sources) {
    esp_err_t err = ESP_OK;
    if (sources & SDF_WAKE_SRC_TIMER) {
        err = sdf_platform_sleep_enable_timer_wakeup(SDF_POWER_DEFAULT_CHECKIN_INTERVAL_MS);
    }
    return err;
}

esp_err_t sdf_platform_sleep_retention_write(const void *data, size_t len) {
#ifndef CONFIG_IDF_TARGET_LINUX
    if (len > sdf_config_get()->retention_size) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_retention_base == NULL) {
        s_retention_base = (uint8_t*)SOC_RTC_DATA_LOW;
    }
    memcpy(s_retention_base, data, len);
#endif
    return ESP_OK;
}

esp_err_t sdf_platform_sleep_retention_read(void *data, size_t len) {
#ifndef CONFIG_IDF_TARGET_LINUX
    if (len > sdf_config_get()->retention_size) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_retention_base == NULL) {
        s_retention_base = (uint8_t*)SOC_RTC_DATA_LOW;
    }
    memcpy(data, s_retention_base, len);
#endif
    return ESP_OK;
}

bool sdf_platform_sleep_retention_valid(void) {
#ifndef CONFIG_IDF_TARGET_LINUX
    if (s_retention_base == NULL) {
        s_retention_base = (uint8_t*)SOC_RTC_DATA_LOW;
    }
    if (s_retention_base == NULL) {
        return false;
    }
    sdf_power_retention_t *retention = (sdf_power_retention_t *)s_retention_base;
    return retention->magic == SDF_POWER_RETENTION_MAGIC;
#else
    return false;
#endif
}