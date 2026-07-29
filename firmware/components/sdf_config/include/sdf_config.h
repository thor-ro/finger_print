#ifndef SDF_CONFIG_H
#define SDF_CONFIG_H

#include "sdkconfig.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief SDF system configuration - centralized runtime configuration.
 *
 * This structure holds all system-wide configuration values.
 * Values are initialized from Kconfig defaults but can be overridden at runtime.
 */
typedef struct {
    /* Fingerprint sensor */
    int fp_uart_port;
    int fp_tx_pin;
    int fp_rx_pin;
    int fp_power_en_pin;
    uint32_t fp_baud_rate;
    uint32_t fp_response_timeout_ms;
    uint16_t fp_rx_buffer_size;
    uint16_t fp_tx_buffer_size;

    /* Fingerprint matching */
    uint32_t match_poll_interval_ms;
    uint32_t match_cooldown_ms;

    /* Security / Biometric */
    uint32_t failed_attempt_threshold;
    uint32_t failed_attempt_window_ms;
    uint32_t lockout_duration_ms;

    /* LED */
    int ws2812_led_gpio;

    /* Battery / ADC */
    int battery_adc_pin;

    /* Power Management */
    uint32_t checkin_interval_ms;
    uint32_t idle_before_sleep_ms;
    uint32_t post_wake_guard_ms;
    uint32_t power_loop_interval_ms;
    uint32_t battery_report_interval_ms;
    uint8_t battery_default_percent;
    int fp_wake_gpio;
    int fp_en_gpio;
    bool enable_light_sleep;
    bool enable_ble_radio_gating;
    bool enable_deep_sleep_fallback;
    bool enable_deep_sleep;
    uint32_t retention_size;
    bool adaptive_checkin;
    bool staged_wake;

    /* Zigbee */
    bool zigbee_enabled;
    uint32_t zigbee_sleep_threshold_ms;

    /* Nuki BLE */
    uint8_t nuki_target_addr_type;
    uint8_t nuki_target_addr[6];
    bool ble_connect_on_demand;

    /* Security */
    uint8_t nonce_replay_window;
    bool require_encrypted_nvs;

    /* Event Router */
    uint32_t event_router_queue_depth;

    /* System */
    uint32_t wdt_timeout_ms;

    /* Enrollment button */
    int enrollment_btn_gpio;

} sdf_config_t;

/**
 * @brief Get default configuration (from Kconfig).
 *
 * Populates config with values from sdkconfig.h defaults.
 *
 * @param config Output configuration structure.
 */
void sdf_config_get_defaults(sdf_config_t *config);

/**
 * @brief Initialize configuration subsystem.
 *
 * Loads defaults, validates, and prepares runtime overrides.
 *
 * @return ESP_OK on success.
 */
esp_err_t sdf_config_init(void);

/**
 * @brief Get pointer to global configuration.
 *
 * @return Pointer to global config (read-only in normal use).
 */
const sdf_config_t *sdf_config_get(void);

/**
 * @brief Get mutable pointer to global configuration.
 *
 * Only available if SDF_CONFIG_ENABLE_RUNTIME_OVERRIDE is enabled.
 * Use with caution - changes take effect immediately.
 *
 * @return Mutable pointer or NULL if overrides disabled.
 */
sdf_config_t *sdf_config_get_mutable(void);

/**
 * @brief Validate configuration values.
 *
 * Checks all fields for reasonable ranges and cross-field consistency.
 *
 * @param config Configuration to validate.
 * @return ESP_OK if valid, ESP_ERR_INVALID_ARG with details logged.
 */
esp_err_t sdf_config_validate(const sdf_config_t *config);

/**
 * @brief Print configuration to log (INFO level).
 *
 * @param config Configuration to dump.
 * @param tag Log tag to use.
 */
void sdf_config_dump(const sdf_config_t *config, const char *tag);

/**
 * @brief Set check-in interval (updates Zigbee if running).
 *
 * @param interval_ms New interval in milliseconds.
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if out of range.
 */
esp_err_t sdf_config_set_checkin_interval(uint32_t interval_ms);

/**
 * @brief Set match poll interval.
 *
 * @param interval_ms New interval in milliseconds.
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if out of range.
 */
esp_err_t sdf_config_set_match_poll_interval(uint32_t interval_ms);

/**
 * @brief Set Zigbee enabled state.
 *
 * @param enabled New enabled state.
 * @return ESP_OK on success.
 */
esp_err_t sdf_config_set_zigbee_enabled(bool enabled);

/**
 * @brief Set battery report interval.
 *
 * @param interval_ms New interval in milliseconds.
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if out of range.
 */
esp_err_t sdf_config_set_battery_report_interval(uint32_t interval_ms);

/**
 * @brief Set idle before sleep.
 *
 * @param interval_ms New interval in milliseconds.
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if out of range.
 */
esp_err_t sdf_config_set_idle_before_sleep(uint32_t interval_ms);

/**
 * @brief Set post-wake guard.
 *
 * @param interval_ms New interval in milliseconds.
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if out of range.
 */
esp_err_t sdf_config_set_post_wake_guard(uint32_t interval_ms);

#ifdef __cplusplus
}
#endif

#endif /* SDF_CONFIG_H */