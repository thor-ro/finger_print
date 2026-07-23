#include "sdf_config.h"

#include <string.h>
#include <stdio.h>

#ifndef CONFIG_IDF_TARGET_LINUX
#include "esp_log.h"
#else
#include <stdio.h>
#define ESP_LOGI(tag, fmt, ...) printf("[I] " fmt "\n", ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) printf("[W] " fmt "\n", ##__VA_ARGS__)
#define ESP_LOGE(tag, fmt, ...) printf("[E] " fmt "\n", ##__VA_ARGS__)
#endif

static const char *TAG = "sdf_config";
static sdf_config_t s_config = {0};
static bool s_initialized = false;

void sdf_config_get_defaults(sdf_config_t *config) {
    if (config == NULL) {
        return;
    }

    memset(config, 0, sizeof(*config));

    /* Fingerprint sensor */
    config->fp_uart_port = 1;
    config->fp_tx_pin = 4;
    config->fp_rx_pin = 5;
    config->fp_power_en_pin = CONFIG_SDF_POWER_FP_EN_GPIO;
    config->fp_baud_rate = 19200;
    config->fp_response_timeout_ms = 12000;
    config->fp_rx_buffer_size = 256;
    config->fp_tx_buffer_size = 256;

    /* Fingerprint matching */
    config->match_poll_interval_ms = 400;
    config->match_cooldown_ms = 3000;

    /* Security / Biometric */
    config->failed_attempt_threshold = CONFIG_SDF_SECURITY_BIOMETRIC_FAIL_THRESHOLD;
    config->failed_attempt_window_ms = CONFIG_SDF_SECURITY_BIOMETRIC_FAIL_WINDOW_MS;
    config->lockout_duration_ms = CONFIG_SDF_SECURITY_BIOMETRIC_LOCKOUT_MS;

    /* LED */
    config->ws2812_led_gpio = CONFIG_SDF_WS2812_LED_GPIO;

    /* Battery / ADC */
    config->battery_adc_pin = 0;

    /* Power Management */
    config->checkin_interval_ms = CONFIG_SDF_POWER_CHECKIN_INTERVAL_MS;
    config->idle_before_sleep_ms = CONFIG_SDF_POWER_IDLE_BEFORE_SLEEP_MS;
    config->post_wake_guard_ms = CONFIG_SDF_POWER_POST_WAKE_GUARD_MS;
    config->power_loop_interval_ms = CONFIG_SDF_POWER_LOOP_INTERVAL_MS;
    config->battery_report_interval_ms = CONFIG_SDF_POWER_BATTERY_REPORT_INTERVAL_MS;
    config->battery_default_percent = CONFIG_SDF_POWER_BATTERY_DEFAULT_PERCENT;
    config->fp_wake_gpio = CONFIG_SDF_POWER_FP_WAKE_GPIO;
    config->fp_en_gpio = CONFIG_SDF_POWER_FP_EN_GPIO;
    config->enable_light_sleep = CONFIG_SDF_POWER_ENABLE_LIGHT_SLEEP;
    config->enable_ble_radio_gating = CONFIG_SDF_POWER_ENABLE_BLE_RADIO_GATING;
    config->enable_deep_sleep_fallback = true;

    /* Zigbee */
#if defined(CONFIG_SDF_ZIGBEE_ENABLE)
    config->zigbee_enabled = CONFIG_SDF_ZIGBEE_ENABLE;
#else
    config->zigbee_enabled = false;
#endif
#if defined(CONFIG_SDF_ZIGBEE_SLEEP_THRESHOLD_MS)
    config->zigbee_sleep_threshold_ms = CONFIG_SDF_ZIGBEE_SLEEP_THRESHOLD_MS;
#else
    config->zigbee_sleep_threshold_ms = 20;
#endif

    /* Nuki BLE */
    config->nuki_target_addr_type = 1; // BLE_ADDR_RANDOM
    memset(config->nuki_target_addr, 0, 6);
    config->ble_connect_on_demand = CONFIG_SDF_BLE_CONNECTION_MODE_ON_DEMAND;

    /* Security */
    config->nonce_replay_window = CONFIG_SDF_SECURITY_NONCE_REPLAY_WINDOW;
    config->require_encrypted_nvs = CONFIG_SDF_SECURITY_REQUIRE_ENCRYPTED_NVS;

    /* System */
    config->wdt_timeout_ms = CONFIG_SDF_PLATFORM_WDT_TIMEOUT_MS;

    /* Enrollment button */
    config->enrollment_btn_gpio = CONFIG_SDF_ENROLLMENT_BTN_GPIO;
}

esp_err_t sdf_config_init(void) {
    if (s_initialized) {
        return ESP_OK;
    }

    sdf_config_get_defaults(&s_config);

#if CONFIG_SDF_CONFIG_VALIDATE_AT_BOOT
    esp_err_t err = sdf_config_validate(&s_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Configuration validation failed");
        return err;
    }
#endif

    s_initialized = true;
    ESP_LOGI(TAG, "Configuration initialized");
    return ESP_OK;
}

const sdf_config_t *sdf_config_get(void) {
    return &s_config;
}

sdf_config_t *sdf_config_get_mutable(void) {
#if CONFIG_SDF_CONFIG_ENABLE_RUNTIME_OVERRIDE
    return &s_config;
#else
    return NULL;
#endif
}

esp_err_t sdf_config_validate(const sdf_config_t *config) {
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    bool valid = true;

    /* Fingerprint */
    if (config->fp_uart_port < 0 || config->fp_uart_port > 2) {
        ESP_LOGE(TAG, "Invalid fp_uart_port: %d", config->fp_uart_port);
        valid = false;
    }
    if (config->fp_tx_pin < 0 || config->fp_tx_pin > 47) {
        ESP_LOGE(TAG, "Invalid fp_tx_pin: %d", config->fp_tx_pin);
        valid = false;
    }
    if (config->fp_rx_pin < 0 || config->fp_rx_pin > 47) {
        ESP_LOGE(TAG, "Invalid fp_rx_pin: %d", config->fp_rx_pin);
        valid = false;
    }
    if (config->fp_baud_rate == 0) {
        ESP_LOGE(TAG, "fp_baud_rate must be > 0");
        valid = false;
    }

    /* Timing */
    if (config->match_poll_interval_ms < 100) {
        ESP_LOGE(TAG, "match_poll_interval_ms too small: %lu", config->match_poll_interval_ms);
        valid = false;
    }
    if (config->match_cooldown_ms < 100) {
        ESP_LOGE(TAG, "match_cooldown_ms too small: %lu", config->match_cooldown_ms);
        valid = false;
    }
    if (config->checkin_interval_ms < 1000) {
        ESP_LOGE(TAG, "checkin_interval_ms too small: %lu", config->checkin_interval_ms);
        valid = false;
    }
    if (config->idle_before_sleep_ms < 100) {
        ESP_LOGE(TAG, "idle_before_sleep_ms too small: %lu", config->idle_before_sleep_ms);
        valid = false;
    }
    if (config->battery_report_interval_ms < 1000) {
        ESP_LOGE(TAG, "battery_report_interval_ms too small: %lu", config->battery_report_interval_ms);
        valid = false;
    }

    /* Security */
    if (config->failed_attempt_threshold == 0) {
        ESP_LOGE(TAG, "failed_attempt_threshold must be > 0");
        valid = false;
    }
    if (config->failed_attempt_window_ms == 0) {
        ESP_LOGE(TAG, "failed_attempt_window_ms must be > 0");
        valid = false;
    }
    if (config->lockout_duration_ms == 0) {
        ESP_LOGE(TAG, "lockout_duration_ms must be > 0");
        valid = false;
    }
    if (config->nonce_replay_window == 0) {
        ESP_LOGE(TAG, "nonce_replay_window must be > 0");
        valid = false;
    }

    /* GPIOs */
    if (config->ws2812_led_gpio < 0 || config->ws2812_led_gpio > 47) {
        ESP_LOGE(TAG, "Invalid ws2812_led_gpio: %d", config->ws2812_led_gpio);
        valid = false;
    }
    if (config->fp_wake_gpio < 0 || config->fp_wake_gpio > 47) {
        ESP_LOGE(TAG, "Invalid fp_wake_gpio: %d", config->fp_wake_gpio);
        valid = false;
    }

    /* Zigbee sleep threshold */
    if (config->zigbee_sleep_threshold_ms == 0) {
        ESP_LOGE(TAG, "zigbee_sleep_threshold_ms must be > 0");
        valid = false;
    }

    /* WDT */
    if (config->wdt_timeout_ms < 5000 || config->wdt_timeout_ms > 60000) {
        ESP_LOGE(TAG, "wdt_timeout_ms out of range: %lu", config->wdt_timeout_ms);
        valid = false;
    }

    return valid ? ESP_OK : ESP_ERR_INVALID_ARG;
}

void sdf_config_dump(const sdf_config_t *config, const char *tag) {
    if (config == NULL || tag == NULL) {
        return;
    }

    ESP_LOGI(tag, "=== SDF Configuration ===");
    ESP_LOGI(tag, "Fingerprint: UART%d TX=%d RX=%d EN=%d @ %u baud",
             config->fp_uart_port, config->fp_tx_pin, config->fp_rx_pin,
             config->fp_power_en_pin, config->fp_baud_rate);
    ESP_LOGI(tag, "Match: poll=%ums cooldown=%ums",
             config->match_poll_interval_ms, config->match_cooldown_ms);
    ESP_LOGI(tag, "Security: threshold=%u window=%ums lockout=%ums nonce_window=%u encrypted_nvs=%d",
             config->failed_attempt_threshold, config->failed_attempt_window_ms,
             config->lockout_duration_ms, config->nonce_replay_window,
             config->require_encrypted_nvs);
    ESP_LOGI(tag, "LED: GPIO=%d", config->ws2812_led_gpio);
    ESP_LOGI(tag, "Power: checkin=%ums idle=%ums wake_guard=%ums loop=%ums batt_report=%ums",
             config->checkin_interval_ms, config->idle_before_sleep_ms,
             config->post_wake_guard_ms, config->power_loop_interval_ms,
             config->battery_report_interval_ms);
    ESP_LOGI(tag, "Sleep: light=%d ble_gate=%d deep_fallback=%d fp_wake=%d fp_en=%d",
             config->enable_light_sleep, config->enable_ble_radio_gating,
             config->enable_deep_sleep_fallback, config->fp_wake_gpio, config->fp_en_gpio);
    ESP_LOGI(tag, "Zigbee: enabled=%d sleep_thresh=%u",
             config->zigbee_enabled, config->zigbee_sleep_threshold_ms);
    ESP_LOGI(tag, "Nuki: addr_type=%d addr=%02X:%02X:%02X:%02X:%02X:%02X on_demand=%d",
             config->nuki_target_addr_type,
             config->nuki_target_addr[0], config->nuki_target_addr[1],
             config->nuki_target_addr[2], config->nuki_target_addr[3],
             config->nuki_target_addr[4], config->nuki_target_addr[5],
             config->ble_connect_on_demand);
    ESP_LOGI(tag, "Button: enrollment=%d", config->enrollment_btn_gpio);
    ESP_LOGI(tag, "WDT: timeout=%ums", config->wdt_timeout_ms);
    ESP_LOGI(tag, "==========================");
}