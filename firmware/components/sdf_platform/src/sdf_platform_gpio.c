#include "sdf_platform_gpio.h"

#include <string.h>

#ifndef CONFIG_IDF_TARGET_LINUX
#include "driver/gpio.h"
#else
#include "sdf_mock_linux_gpio.h"
#endif
#include "esp_log.h"

static const char *TAG = "sdf_platform_gpio";
static bool s_isr_service_installed = false;

esp_err_t sdf_platform_gpio_configure_wake(gpio_num_t gpio_num,
                                           gpio_int_type_t intr_type,
                                           bool pull_up,
                                           bool pull_down,
                                           sdf_platform_gpio_isr_t isr_handler,
                                           void *isr_arg) {
    gpio_config_t io_config = {
        .pin_bit_mask = (1ULL << gpio_num),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = pull_up ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = pull_down ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE,
        .intr_type = intr_type,
    };

    esp_err_t err = gpio_config(&io_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure GPIO %d: %s", gpio_num, esp_err_to_name(err));
        return err;
    }

    if (isr_handler != NULL) {
        if (!s_isr_service_installed) {
            err = gpio_install_isr_service(0);
            if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
                ESP_LOGE(TAG, "Failed to install ISR service: %s", esp_err_to_name(err));
                return err;
            }
            s_isr_service_installed = true;
        }

        err = gpio_isr_handler_add(gpio_num, (gpio_isr_t)isr_handler, isr_arg);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to add ISR handler for GPIO %d: %s", gpio_num, esp_err_to_name(err));
            return err;
        }
    }

    return ESP_OK;
}

esp_err_t sdf_platform_gpio_configure_output(gpio_num_t gpio_num, int initial_level) {
    gpio_config_t io_config = {
        .pin_bit_mask = (1ULL << gpio_num),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t err = gpio_config(&io_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure GPIO %d as output: %s", gpio_num, esp_err_to_name(err));
        return err;
    }

    return gpio_set_level(gpio_num, initial_level);
}

esp_err_t sdf_platform_gpio_set_level(gpio_num_t gpio_num, int level) {
    return gpio_set_level(gpio_num, level);
}

int sdf_platform_gpio_get_level(gpio_num_t gpio_num) {
    return gpio_get_level(gpio_num);
}

esp_err_t sdf_platform_gpio_install_isr_service(int flags) {
    if (s_isr_service_installed) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = gpio_install_isr_service(flags);
    if (err == ESP_OK) {
        s_isr_service_installed = true;
    }
    return err;
}

void sdf_platform_gpio_uninstall_isr_service(void) {
    if (s_isr_service_installed) {
        gpio_uninstall_isr_service();
        s_isr_service_installed = false;
    }
}

esp_err_t sdf_platform_gpio_isr_handler_add(gpio_num_t gpio_num,
                                            gpio_isr_t isr_handler,
                                            void *arg) {
    return gpio_isr_handler_add(gpio_num, isr_handler, arg);
}

esp_err_t sdf_platform_gpio_isr_handler_remove(gpio_num_t gpio_num) {
    return gpio_isr_handler_remove(gpio_num);
}

bool sdf_platform_gpio_is_rtc_capable(gpio_num_t gpio_num) {
    // On ESP32-C6, GPIOs 0-7 are RTC-capable
    return gpio_num < GPIO_NUM_8;
}