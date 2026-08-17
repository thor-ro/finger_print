#include "sdf_services_internal.h"
#include "sdf_drivers.h"
#include "sdf_platform.h"
#include "sdf_config.h"
#include "sdf_event_router.h"

#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sdkconfig.h"

#ifndef CONFIG_IDF_TARGET_LINUX
#include "button_gpio.h"
#include "iot_button.h"
#else
#include "sdf_mock_linux_gpio.h"
#endif

static const char *TAG = "sdf_services_button";

#ifndef CONFIG_IDF_TARGET_LINUX
static button_handle_t s_btn_handle = NULL;
#endif

static void sdf_button_single_click_cb(void *arg, void *usr_data) {
    (void)arg;
    (void)usr_data;
    sdf_event_router_event_t evt = {
        .type = SDF_EVENT_ROUTER_BUTTON_PRESS,
        .priority = SDF_EVENT_ROUTER_PRIO_HIGH,
        .timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL),
        .payload.button = {
            .press_type = SDF_EVENT_ROUTER_BUTTON_PRESS_SINGLE,
            .press_duration_ms = 0,
        },
    };
    sdf_event_router_emit(&evt, 0);
}

static void sdf_button_cb(void *arg, void *usr_data) {
    (void)arg;
    uint8_t press_type = (uint8_t)(uintptr_t)usr_data;
    uint32_t press_duration_ms = 0;
    if (press_type == SDF_EVENT_ROUTER_BUTTON_PRESS_LONG) {
        press_duration_ms = 8000;
    }
    sdf_event_router_event_t evt = {
        .type = SDF_EVENT_ROUTER_BUTTON_PRESS,
        .priority = SDF_EVENT_ROUTER_PRIO_HIGH,
        .timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL),
        .payload.button = {
            .press_type = press_type,
            .press_duration_ms = press_duration_ms,
        },
    };
    sdf_event_router_emit(&evt, 0);
}

esp_err_t sdf_button_init(void) {
    sdf_services_state_t *s = sdf_services_state();
    if (s == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

#ifndef CONFIG_IDF_TARGET_LINUX
    if (s->config.enrollment_btn_gpio >= 0) {
        if (s_btn_handle != NULL) {
            iot_button_delete(s_btn_handle);
            s_btn_handle = NULL;
        }

        button_config_t btn_config = {
            .long_press_time = 3000,
            .short_press_time = 180,
        };
        button_gpio_config_t gpio_config = {
            .gpio_num = s->config.enrollment_btn_gpio,
            .active_level = 0,
            .enable_power_save = true,
            .disable_pull = false,
        };

        if (iot_button_new_gpio_device(&btn_config, &gpio_config, &s_btn_handle) == ESP_OK) {
            iot_button_register_cb(s_btn_handle, BUTTON_SINGLE_CLICK, NULL,
                                   sdf_button_single_click_cb, NULL);

            iot_button_register_cb(s_btn_handle, BUTTON_DOUBLE_CLICK, NULL,
                                   sdf_button_cb,
                                   (void *)(uintptr_t)SDF_EVENT_ROUTER_BUTTON_PRESS_DOUBLE);

            button_event_args_t arg_8s = {.long_press = {.press_time = 8000}};
            iot_button_register_cb(s_btn_handle, BUTTON_LONG_PRESS_START, &arg_8s,
                                   sdf_button_cb,
                                   (void *)(uintptr_t)SDF_EVENT_ROUTER_BUTTON_PRESS_LONG);

            ESP_LOGI(TAG, "Button initialized on GPIO %d (power save enabled)", (int)s->config.enrollment_btn_gpio);
        } else {
            ESP_LOGW(TAG, "Failed to create enrollment iot_button");
            return ESP_FAIL;
        }
    }
#endif

    return ESP_OK;
}

esp_err_t sdf_button_deinit(void) {
#ifndef CONFIG_IDF_TARGET_LINUX
    if (s_btn_handle != NULL) {
        iot_button_delete(s_btn_handle);
        s_btn_handle = NULL;
    }
#endif
    return ESP_OK;
}