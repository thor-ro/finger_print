# Specification: sdf_config Consolidated Configuration

## Scope
Complete specification for `sdf_config` as single source of truth for all SDF runtime configuration.

## Requirements

### R-1: Unified Configuration Struct
All 50+ configuration fields accessible via single `sdf_config_t` struct.

### R-2: Single Default Provider
`sdf_config_get_defaults()` populates ALL fields from Kconfig.

### R-3: Centralized Validation
`sdf_config_validate()` validates ALL fields with meaningful error messages.

### R-4: Runtime Override
`sdf_config_get_mutable()` enables runtime reconfiguration.

### R-5: Component Migration
All 3 components (`sdf_power`, `sdf_services`, `sdf_app`) use `sdf_config_get()`.

## Field Specification

### Power Management (12 fields)
| Field | Type | Kconfig Source | Range | Default |
|-------|------|----------------|-------|---------|
| `checkin_interval_ms` | `uint32_t` | `CONFIG_SDF_POWER_CHECKIN_INTERVAL_MS` | 1000..600000 | 15000 |
| `idle_before_sleep_ms` | `uint32_t` | `CONFIG_SDF_POWER_IDLE_BEFORE_SLEEP_MS` | 500..600000 | 5000 |
| `post_wake_guard_ms` | `uint32_t` | `CONFIG_SDF_POWER_POST_WAKE_GUARD_MS` | 100..60000 | 1500 |
| `loop_interval_ms` | `uint32_t` | `CONFIG_SDF_POWER_LOOP_INTERVAL_MS` | 50..10000 | 250 |
| `battery_report_interval_ms` | `uint32_t` | `CONFIG_SDF_POWER_BATTERY_REPORT_INTERVAL_MS` | 1000..86400000 | 60000 |
| `battery_default_percent` | `uint8_t` | `CONFIG_SDF_POWER_BATTERY_DEFAULT_PERCENT` | 0..100 | 100 |
| `fp_wake_gpio` | `int` | `CONFIG_SDF_POWER_FP_WAKE_GPIO` | -1..30 | 3 |
| `enable_light_sleep` | `bool` | `CONFIG_SDF_POWER_ENABLE_LIGHT_SLEEP` | - | true |
| `enable_ble_radio_gating` | `bool` | `CONFIG_SDF_POWER_ENABLE_BLE_RADIO_GATING` | - | true |
| `enable_deep_sleep_fallback` | `bool` | (new) | - | true |
| `fp_power_en_gpio` | `int` | `CONFIG_SDF_POWER_FP_EN_GPIO` | -1..30 | 2 |

### Fingerprint Services (11 fields)
| Field | Type | Kconfig Source | Range | Default |
|-------|------|----------------|-------|---------|
| `match_poll_interval_ms` | `uint32_t` | `CONFIG_SDF_SERVICES_MATCH_POLL_MS` | 100..2000 | 400 |
| `match_cooldown_ms` | `uint32_t` | `CONFIG_SDF_SERVICES_MATCH_COOLDOWN_MS` | 100..60000 | 5000 |
| `failed_attempt_threshold` | `uint32_t` | `CONFIG_SDF_SECURITY_BIOMETRIC_FAIL_THRESHOLD` | 1..100 | 5 |
| `failed_attempt_window_ms` | `uint32_t` | `CONFIG_SDF_SECURITY_BIOMETRIC_FAIL_WINDOW_MS` | 1000..3600000 | 60000 |
| `lockout_duration_ms` | `uint32_t` | `CONFIG_SDF_SECURITY_BIOMETRIC_LOCKOUT_MS` | 1000..3600000 | 120000 |
| `fp_uart_port` | `int` | (new) | - | 1 |
| `fp_tx_pin` | `int` | (new) | - | 4 |
| `fp_rx_pin` | `int` | (new) | - | 5 |
| `fp_baud_rate` | `uint32_t` | (new) | - | 19200 |
| `enrollment_btn_gpio` | `int` | `CONFIG_SDF_ENROLLMENT_BTN_GPIO` | -1..30 | 14 |
| `ws2812_led_gpio` | `int` | `CONFIG_SDF_WS2812_LED_GPIO` | -1..30 | 8 |

### Zigbee (3 fields)
| Field | Type | Kconfig Source | Range | Default |
|-------|------|----------------|-------|---------|
| `zigbee_enabled` | `bool` | `CONFIG_SDF_ZIGBEE_ENABLED` | - | true |
| `zigbee_sleep_threshold_ms` | `uint32_t` | (new) | - | 30000 |
| `zigbee_install_code_policy` | `bool` | `CONFIG_SDF_ZIGBEE_INSTALL_CODE_POLICY` | - | false |

### Nuki BLE (4 fields)
| Field | Type | Kconfig Source | Range | Default |
|-------|------|----------------|-------|---------|
| `nuki_target_addr_type` | `uint8_t` | (new) | - | 0 |
| `nuki_target_addr` | `uint8_t[6]` | (new) | - | all zero |
| `ble_connect_on_demand` | `bool` | `CONFIG_SDF_BLE_CONNECT_ON_DEMAND` | - | true |

### Security (3 fields)
| Field | Type | Kconfig Source | Range | Default |
|-------|------|----------------|-------|---------|
| `nonce_replay_window` | `uint8_t` | `CONFIG_SDF_SECURITY_NONCE_REPLAY_WINDOW` | 1..16 | 8 |
| `require_encrypted_nvs` | `bool` | `CONFIG_SDF_SECURITY_REQUIRE_ENCRYPTED_NVS` | - | true |

### System (2 fields)
| Field | Type | Kconfig Source | Range | Default |
|-------|------|----------------|-------|---------|
| `wdt_timeout_ms` | `uint32_t` | `CONFIG_SDF_WDT_TIMEOUT_MS` | 1000..60000 | 15000 |

## API Specification

### sdf_config.h
```c
// Existing - unchanged
void sdf_config_get_defaults(sdf_config_t *config);
esp_err_t sdf_config_init(void);
const sdf_config_t *sdf_config_get(void);
sdf_config_t *sdf_config_get_mutable(void);
esp_err_t sdf_config_validate(const sdf_config_t *config);
void sdf_config_dump(const sdf_config_t *config, const char *tag);

// NEW: Setters for validated runtime changes
esp_err_t sdf_config_set_checkin_interval(uint32_t ms);
esp_err_t sdf_config_set_match_poll_interval(uint32_t ms);
esp_err_t sdf_config_set_zigbee_enabled(bool enabled);
// ... one per critical field
```

### Validation Rules
- All range checks from tables above
- Cross-field: `idle_before_sleep_ms > post_wake_guard_ms`
- Cross-field: `battery_report_interval_ms >= loop_interval_ms * 2`
- Zigbee: if `zigbee_enabled && !CONFIG_ZB_ENABLED` → error

## Acceptance Criteria

1. **Struct completeness**: `sizeof(sdf_config_t)` includes all 35+ fields
2. **Defaults**: `sdf_config_get_defaults()` produces valid config passing `validate()`
3. **Validation**: Invalid config returns `ESP_ERR_INVALID_ARG` with specific field in log
4. **Migration**: `sdf_power` init uses `sdf_config_get()->checkin_interval_ms` etc.
5. **Override**: `sdf_config_set_checkin_interval(30000)` updates running value and Zigbee
6. **Kconfig**: Unified menu "SDF Configuration" with submenus for each domain