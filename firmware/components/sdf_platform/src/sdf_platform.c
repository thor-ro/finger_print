#include "sdf_platform.h"

#include <string.h>

#ifndef CONFIG_IDF_TARGET_LINUX
#include "esp_log.h"
#else
#include "sdf_mock_linux_gpio.h"
#include "sdf_mock_linux_sleep.h"
#include "sdf_mock_linux_time.h"
#include "sdf_mock_linux_nvs.h"
#endif

static const char *TAG = "sdf_platform";
static bool s_initialized = false;

esp_err_t sdf_platform_init(void) {
    if (s_initialized) {
        return ESP_OK;
    }

#ifndef CONFIG_IDF_TARGET_LINUX
    esp_err_t err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to install GPIO ISR service: %s", esp_err_to_name(err));
        return err;
    }
#endif

    s_initialized = true;
    ESP_LOGI(TAG, "Platform initialized");
    return ESP_OK;
}

void sdf_platform_deinit(void) {
    if (!s_initialized) {
        return;
    }

#ifndef CONFIG_IDF_TARGET_LINUX
    gpio_uninstall_isr_service();
#endif

    s_initialized = false;
    ESP_LOGI(TAG, "Platform deinitialized");
}

bool sdf_platform_is_initialized(void) {
    return s_initialized;
}