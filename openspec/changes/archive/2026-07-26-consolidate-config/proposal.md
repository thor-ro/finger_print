# Proposal: Consolidate Configuration Management

## Summary

Make `sdf_config` the single source of truth for all runtime configuration. Remove duplicate `get_default_*_config()` functions from `sdf_power`, `sdf_services`, and `sdf_app`. All components query `sdf_config_get()` at initialization.

## Problem

Currently configuration defaults are scattered:

| Component | Function | Duplicates |
|-----------|----------|------------|
| `sdf_power` | `sdf_power_get_default_power_config()` | 12 Kconfig values |
| `sdf_services` | `sdf_services_get_default_config()` | 18 Kconfig values |
| `sdf_app` | Inline defaults in `sdf_app_init()` | 20+ Kconfig values |
| `sdf_config` | `sdf_config_get_defaults()` | **Single source** |

This causes:
- **Drift risk**: Same Kconfig used in 3+ places
- **Testing difficulty**: Must mock multiple default functions
- **Runtime override impossible**: `sdf_config_get_mutable()` unused
- **Validation gaps**: `sdf_config_validate()` only validates `sdf_config_t` struct

## Solution

### 1. Extend `sdf_config_t` to Include All Domains

```c
// sdf_config.h - ADD these sections
typedef struct {
    // ... existing fields ...
    
    // Power Management (from sdf_power_get_default_power_config)
    uint32_t checkin_interval_ms;
    uint32_t idle_before_sleep_ms;
    uint32_t post_wake_guard_ms;
    uint32_t loop_interval_ms;
    uint32_t battery_report_interval_ms;
    bool enable_light_sleep;
    bool enable_ble_radio_gating;
    bool enable_deep_sleep_fallback;
    int fp_wake_gpio;
    
    // Fingerprint Services (from sdf_services_get_default_config)
    uint32_t match_poll_interval_ms;
    uint32_t match_cooldown_ms;
    uint32_t failed_attempt_threshold;
    uint32_t failed_attempt_window_ms;
    uint32_t lockout_duration_ms;
    int wake_gpio;
    int power_en_gpio;
    int enrollment_btn_gpio;
    int ws2812_led_gpio;
    int battery_adc_pin;
    
    // Zigbee (from sdf_app_init)
    bool zigbee_enabled;
    uint32_t zigbee_sleep_threshold_ms;
    
    // Nuki BLE
    uint8_t nuki_target_addr_type;
    uint8_t nuki_target_addr[6];
    bool ble_connect_on_demand;
    
    // Security
    uint8_t nonce_replay_window;
    bool require_encrypted_nvs;
    
    // System
    uint32_t wdt_timeout_ms;
} sdf_config_t;
```

### 2. Remove Duplicate Default Functions

**Delete:**
- `sdf_power_get_default_power_config()` → use `sdf_config_get()->power_*`
- `sdf_services_get_default_config()` → use `sdf_config_get()->services_*`
- Inline defaults in `sdf_app_init()` → use `sdf_config_get()`

**Keep:**
- `sdf_config_get_defaults()` - populates struct from Kconfig
- `sdf_config_init()` - initializes subsystem, validates
- `sdf_config_get()` / `sdf_config_get_mutable()` - runtime access

### 3. Update Component Initialization

```c
// sdf_power_init_power_manager()
const sdf_config_t *cfg = sdf_config_get();
sdf_power_manager_config_t pwr_cfg = {
    .checkin_interval_ms = cfg->checkin_interval_ms,
    .idle_before_sleep_ms = cfg->idle_before_sleep_ms,
    // ... map all fields
};

// sdf_services_init()
const sdf_config_t *cfg = sdf_config_get();
sdf_services_config_t svc_cfg = {
    .match_poll_interval_ms = cfg->match_poll_interval_ms,
    .match_cooldown_ms = cfg->match_cooldown_ms,
    // ... map all fields
};

// sdf_app_init() - no inline defaults, all from sdf_config_get()
```

### 4. Enable Runtime Override

```c
// Currently unused - now functional
sdf_config_t *cfg = sdf_config_get_mutable();
if (cfg) {
    cfg->checkin_interval_ms = 30000;  // Change at runtime
    cfg->enable_light_sleep = false;   // Disable for debugging
}
```

### 5. Centralized Validation

```c
// sdf_config_validate() validates ALL domains
esp_err_t sdf_config_validate(const sdf_config_t *cfg) {
    // Power
    if (cfg->checkin_interval_ms < 1000 || cfg->checkin_interval_ms > 600000) return ESP_ERR_INVALID_ARG;
    // Services
    if (cfg->match_poll_interval_ms < 100 || cfg->match_poll_interval_ms > 10000) return ESP_ERR_INVALID_ARG;
    // Zigbee
    if (cfg->zigbee_sleep_threshold_ms < 5000) return ESP_ERR_INVALID_ARG;
    // ... all validations in ONE place
}
```

## Architecture Changes

### Files Modified

| File | Change |
|------|--------|
| `sdf_config/include/sdf_config.h` | Add power/services/zigbee/ble/security fields to `sdf_config_t` |
| `sdf_config/src/sdf_config.c` | Extend `get_defaults()`, `validate()`, `dump()` |
| `sdf_power/src/sdf_power.c` | Remove `sdf_power_get_default_power_config()`, use `sdf_config_get()` |
| `sdf_services/src/sdf_services.c` | Remove `sdf_services_get_default_config()`, use `sdf_config_get()` |
| `sdf_app/src/sdf_app.c` | Remove inline defaults, use `sdf_config_get()` |
| All Kconfig files | Ensure consistent naming (prefix `CONFIG_SDF_`) |

### Kconfig Consolidation

Current scattered Kconfig keys → unified under `menu "SDF Configuration"`:

```
menu "SDF Configuration"
    menu "Power Management"
        CONFIG_SDF_POWER_CHECKIN_INTERVAL_MS
        CONFIG_SDF_POWER_IDLE_BEFORE_SLEEP_MS
        ...
    endmenu
    menu "Fingerprint Services"
        CONFIG_SDF_SERVICES_MATCH_POLL_MS
        CONFIG_SDF_SERVICES_MATCH_COOLDOWN_MS
        ...
    endmenu
    menu "Zigbee"
        CONFIG_SDF_ZIGBEE_ENABLED
        ...
    endmenu
    menu "Nuki BLE"
        CONFIG_SDF_NUKI_TARGET_ADDR_TYPE
        ...
    endmenu
    menu "Security"
        CONFIG_SDF_SECURITY_NONCE_REPLAY_WINDOW
        ...
    endmenu
endmenu
```

## Benefits

1. **Single source of truth**: All defaults in `sdf_config_get_defaults()`
2. **Runtime reconfiguration**: `sdf_config_get_mutable()` now works
3. **Complete validation**: One `validate()` catches all mismatches
4. **Easier testing**: One mock for all config
5. **Documentation**: `sdf_config_dump()` shows entire system config
6. **CI validation**: Compile-time check for Kconfig→struct mapping

## Migration Steps

1. Extend `sdf_config_t` with all fields (additive, no removals)
2. Implement mapping in `sdf_config_get_defaults()`
3. Add validation rules for new fields
4. Update `sdf_power` to use `sdf_config_get()`
5. Update `sdf_services` to use `sdf_config_get()`
6. Update `sdf_app` to use `sdf_config_get()`
6. Remove old `get_default_*_config()` functions
7. Consolidate Kconfig menus
8. Update documentation (sdf_sas.md §8, §15)

## Acceptance Criteria

- [ ] `sdf_config_t` contains all 50+ configuration fields
- [ ] `sdf_config_validate()` passes for default config
- [ ] `sdf_power_init_power_manager()` uses `sdf_config_get()`
- [ ] `sdf_services_init()` uses `sdf_config_get()`
- [ ] `sdf_app_init()` has zero inline Kconfig references
- [ ] `sdf_config_get_mutable()` allows runtime changes
- [ ] All unit tests pass
- [ ] Documentation updated per AGENTS.md

## Risks

- **Large struct**: `sdf_config_t` grows to ~80 fields. Mitigation: logical grouping in comments.
- **Migration scope**: 3 components touched. Mitigation: additive changes first, then removals.
- **Kconfig renaming**: Breaks existing sdkconfig. Mitigation: provide migration script.