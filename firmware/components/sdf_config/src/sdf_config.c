#include "sdf_config.h"
#include "sdf_protocol_zigbee.h"

#include <string.h>
#include <stdio.h>
#include <inttypes.h>

#include "nvs.h"
#include "nvs_flash.h"

#ifndef CONFIG_IDF_TARGET_LINUX
#include "esp_log.h"
#else
#include <stdio.h>
#define ESP_LOGI(tag, fmt, ...) printf("[I] " fmt "\n", ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) printf("[W] " fmt "\n", ##__VA_ARGS__)
#define ESP_LOGE(tag, fmt, ...) printf("[E] " fmt "\n", ##__VA_ARGS__)
#endif

/* Runtime overrides (see sdf_config_save()/sdf_config_init()) are persisted
 * as a single blob holding the whole sdf_config_t. That struct is plain
 * data (no pointers), so this is safe as long as its layout doesn't change
 * between the write and the read - sdf_config_init() below guards against a
 * stale/mismatched blob (e.g. after a firmware upgrade that resized the
 * struct) by checking the read-back length before trusting it. */
#define SDF_CONFIG_NVS_NAMESPACE "sdf_cfg"
#define SDF_CONFIG_NVS_KEY "cfg"

static const char *TAG = "sdf_config";
static sdf_config_t s_config = {0};
static bool s_initialized = false;

void sdf_config_get_defaults(sdf_config_t *config) {
    if (config == NULL) {
        return;
    }

    memset(config, 0, sizeof(*config));

    /* Fingerprint sensor */
    config->fp_uart_port = CONFIG_SDF_FP_UART_PORT;
    config->fp_tx_pin = CONFIG_SDF_FP_TX_PIN;
    config->fp_rx_pin = CONFIG_SDF_FP_RX_PIN;
    config->fp_power_en_pin = CONFIG_SDF_POWER_FP_EN_GPIO;
    config->fp_baud_rate = CONFIG_SDF_FP_BAUD_RATE;
    config->fp_response_timeout_ms = CONFIG_SDF_FP_RESPONSE_TIMEOUT_MS;
    config->fp_rx_buffer_size = CONFIG_SDF_FP_RX_BUFFER_SIZE;
    config->fp_tx_buffer_size = CONFIG_SDF_FP_TX_BUFFER_SIZE;

    /* Fingerprint matching */
    config->match_poll_interval_ms = CONFIG_SDF_MATCH_POLL_INTERVAL_MS;
    config->match_cooldown_ms = CONFIG_SDF_MATCH_COOLDOWN_MS;

    /* Security / Biometric */
    config->failed_attempt_threshold = CONFIG_SDF_SECURITY_BIOMETRIC_FAIL_THRESHOLD;
    config->failed_attempt_window_ms = CONFIG_SDF_SECURITY_BIOMETRIC_FAIL_WINDOW_MS;
    config->lockout_duration_ms = CONFIG_SDF_SECURITY_BIOMETRIC_LOCKOUT_MS;

    /* LED */
    config->ws2812_led_gpio = CONFIG_SDF_WS2812_LED_GPIO;

    /* Battery / ADC */
    config->battery_adc_pin = CONFIG_SDF_BATTERY_ADC_PIN;

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
    config->enable_deep_sleep = CONFIG_SDF_POWER_ENABLE_DEEP_SLEEP;
    config->retention_size = CONFIG_SDF_POWER_RETENTION_SIZE;
    config->adaptive_checkin = CONFIG_SDF_POWER_ADAPTIVE_CHECKIN;
    config->staged_wake = CONFIG_SDF_POWER_STAGED_WAKE;

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
    config->nuki_state_poll_interval_ms = CONFIG_SDF_POWER_NUKI_STATE_POLL_INTERVAL_MS;

    /* Security */
    config->nonce_replay_window = CONFIG_SDF_SECURITY_NONCE_REPLAY_WINDOW;
    config->require_encrypted_nvs = CONFIG_SDF_SECURITY_REQUIRE_ENCRYPTED_NVS;

    /* System */
    config->wdt_timeout_ms = CONFIG_SDF_PLATFORM_WDT_TIMEOUT_MS;

    /* Enrollment button */
    config->enrollment_btn_gpio = CONFIG_SDF_ENROLLMENT_BTN_GPIO;

    /* Event Router */
    config->event_router_queue_depth = CONFIG_SDF_EVENT_ROUTER_QUEUE_DEPTH;
}

/* Reads a previously sdf_config_save()'d blob into *out. Returns ESP_OK only
 * if a full-size blob was found and read - ESP_ERR_NVS_NOT_FOUND on first
 * boot (namespace/key never written) is expected and not logged as an
 * error; anything else (including a size mismatch, which most likely means
 * a firmware upgrade changed sdf_config_t's layout) is treated the same way
 * by the caller - discard it and fall back to Kconfig defaults - but is
 * worth a warning since it means the previously persisted overrides were
 * silently dropped. */
static esp_err_t sdf_config_load_persisted(sdf_config_t *out) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(SDF_CONFIG_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return err;
    }

    sdf_config_t loaded;
    size_t len = sizeof(loaded);
    err = nvs_get_blob(handle, SDF_CONFIG_NVS_KEY, &loaded, &len);
    nvs_close(handle);

    if (err != ESP_OK) {
        return err;
    }
    if (len != sizeof(loaded)) {
        ESP_LOGW(TAG, "Persisted config size mismatch (got %u, expected %u), discarding",
                 (unsigned)len, (unsigned)sizeof(loaded));
        return ESP_ERR_INVALID_SIZE;
    }

    *out = loaded;
    return ESP_OK;
}

esp_err_t sdf_config_init(void) {
    if (s_initialized) {
        return ESP_OK;
    }

    sdf_config_get_defaults(&s_config);

    sdf_config_t persisted;
    esp_err_t load_err = sdf_config_load_persisted(&persisted);
    if (load_err == ESP_OK) {
        if (sdf_config_validate(&persisted) == ESP_OK) {
            ESP_LOGI(TAG, "Loaded persisted runtime config overrides from NVS");
            s_config = persisted;
        } else {
            ESP_LOGW(TAG, "Persisted config failed validation, using Kconfig defaults");
        }
    } else if (load_err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Could not read persisted config: %s", esp_err_to_name(load_err));
    }

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
        ESP_LOGE(TAG, "match_poll_interval_ms too small: %" PRIu32, config->match_poll_interval_ms);
        valid = false;
    }
    if (config->match_cooldown_ms < 100) {
        ESP_LOGE(TAG, "match_cooldown_ms too small: %" PRIu32, config->match_cooldown_ms);
        valid = false;
    }
    if (config->checkin_interval_ms < 1000) {
        ESP_LOGE(TAG, "checkin_interval_ms too small: %" PRIu32, config->checkin_interval_ms);
        valid = false;
    }
    if (config->idle_before_sleep_ms < 100) {
        ESP_LOGE(TAG, "idle_before_sleep_ms too small: %" PRIu32, config->idle_before_sleep_ms);
        valid = false;
    }
    if (config->battery_report_interval_ms < 1000) {
        ESP_LOGE(TAG, "battery_report_interval_ms too small: %" PRIu32, config->battery_report_interval_ms);
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
        ESP_LOGE(TAG, "wdt_timeout_ms out of range: %" PRIu32, config->wdt_timeout_ms);
        valid = false;
    }

    /* Event Router */
    if (config->event_router_queue_depth < 8 || config->event_router_queue_depth > 64) {
        ESP_LOGE(TAG, "event_router_queue_depth out of range: %" PRIu32, config->event_router_queue_depth);
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
    ESP_LOGI(tag, "Event Router: queue_depth=%u", config->event_router_queue_depth);
    ESP_LOGI(tag, "WDT: timeout=%ums", config->wdt_timeout_ms);
    ESP_LOGI(tag, "==========================");
}

esp_err_t sdf_config_set_checkin_interval(uint32_t interval_ms) {
    if (interval_ms < 1000 || interval_ms > 600000) {
        return ESP_ERR_INVALID_ARG;
    }
    sdf_config_t *cfg = sdf_config_get_mutable();
    if (!cfg) {
        return ESP_ERR_INVALID_STATE;
    }
    cfg->checkin_interval_ms = interval_ms;
    /* Propagate to Zigbee if running */
    if (sdf_protocol_zigbee_is_enabled() && sdf_protocol_zigbee_is_ready()) {
        sdf_protocol_zigbee_set_checkin_interval_ms(interval_ms);
    }
    return ESP_OK;
}

esp_err_t sdf_config_set_match_poll_interval(uint32_t interval_ms) {
    if (interval_ms < 100 || interval_ms > 10000) {
        return ESP_ERR_INVALID_ARG;
    }
    sdf_config_t *cfg = sdf_config_get_mutable();
    if (!cfg) {
        return ESP_ERR_INVALID_STATE;
    }
    cfg->match_poll_interval_ms = interval_ms;
    return ESP_OK;
}

esp_err_t sdf_config_set_zigbee_enabled(bool enabled) {
    sdf_config_t *cfg = sdf_config_get_mutable();
    if (!cfg) {
        return ESP_ERR_INVALID_STATE;
    }
    cfg->zigbee_enabled = enabled;
    return ESP_OK;
}

esp_err_t sdf_config_set_battery_report_interval(uint32_t interval_ms) {
    if (interval_ms < 1000 || interval_ms > 86400000) {
        return ESP_ERR_INVALID_ARG;
    }
    sdf_config_t *cfg = sdf_config_get_mutable();
    if (!cfg) {
        return ESP_ERR_INVALID_STATE;
    }
    cfg->battery_report_interval_ms = interval_ms;
    return ESP_OK;
}

esp_err_t sdf_config_set_idle_before_sleep(uint32_t interval_ms) {
    if (interval_ms < 500 || interval_ms > 600000) {
        return ESP_ERR_INVALID_ARG;
    }
    sdf_config_t *cfg = sdf_config_get_mutable();
    if (!cfg) {
        return ESP_ERR_INVALID_STATE;
    }
    cfg->idle_before_sleep_ms = interval_ms;
    return ESP_OK;
}

esp_err_t sdf_config_set_post_wake_guard(uint32_t interval_ms) {
    if (interval_ms < 100 || interval_ms > 60000) {
        return ESP_ERR_INVALID_ARG;
    }
    sdf_config_t *cfg = sdf_config_get_mutable();
    if (!cfg) {
        return ESP_ERR_INVALID_STATE;
    }
    cfg->post_wake_guard_ms = interval_ms;
    return ESP_OK;
}

esp_err_t sdf_config_set_nuki_state_poll_interval(uint32_t interval_ms) {
    if (interval_ms < 1000 || interval_ms > 86400000) {
        return ESP_ERR_INVALID_ARG;
    }
    sdf_config_t *cfg = sdf_config_get_mutable();
    if (!cfg) {
        return ESP_ERR_INVALID_STATE;
    }
    cfg->nuki_state_poll_interval_ms = interval_ms;
    return ESP_OK;
}

esp_err_t sdf_config_set_battery_default_percent(uint8_t percent) {
    if (percent > 100) {
        return ESP_ERR_INVALID_ARG;
    }
    sdf_config_t *cfg = sdf_config_get_mutable();
    if (!cfg) {
        return ESP_ERR_INVALID_STATE;
    }
    cfg->battery_default_percent = percent;
    return ESP_OK;
}

esp_err_t sdf_config_set_power_loop_interval(uint32_t interval_ms) {
    if (interval_ms < 100 || interval_ms > 600000) {
        return ESP_ERR_INVALID_ARG;
    }
    sdf_config_t *cfg = sdf_config_get_mutable();
    if (!cfg) {
        return ESP_ERR_INVALID_STATE;
    }
    cfg->power_loop_interval_ms = interval_ms;
    return ESP_OK;
}

esp_err_t sdf_config_set_ble_connect_on_demand(bool enabled) {
    sdf_config_t *cfg = sdf_config_get_mutable();
    if (!cfg) {
        return ESP_ERR_INVALID_STATE;
    }
    cfg->ble_connect_on_demand = enabled;
    return ESP_OK;
}

esp_err_t sdf_config_set_failed_attempt_threshold(uint32_t threshold) {
    if (threshold == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    sdf_config_t *cfg = sdf_config_get_mutable();
    if (!cfg) {
        return ESP_ERR_INVALID_STATE;
    }
    cfg->failed_attempt_threshold = threshold;
    return ESP_OK;
}

esp_err_t sdf_config_set_failed_attempt_window(uint32_t window_ms) {
    if (window_ms == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    sdf_config_t *cfg = sdf_config_get_mutable();
    if (!cfg) {
        return ESP_ERR_INVALID_STATE;
    }
    cfg->failed_attempt_window_ms = window_ms;
    return ESP_OK;
}

esp_err_t sdf_config_set_lockout_duration(uint32_t duration_ms) {
    if (duration_ms == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    sdf_config_t *cfg = sdf_config_get_mutable();
    if (!cfg) {
        return ESP_ERR_INVALID_STATE;
    }
    cfg->lockout_duration_ms = duration_ms;
    return ESP_OK;
}

esp_err_t sdf_config_save(void) {
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(SDF_CONFIG_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Config save: nvs_open failed: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_blob(handle, SDF_CONFIG_NVS_KEY, &s_config, sizeof(s_config));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Config save failed: %s", esp_err_to_name(err));
    }
    return err;
}