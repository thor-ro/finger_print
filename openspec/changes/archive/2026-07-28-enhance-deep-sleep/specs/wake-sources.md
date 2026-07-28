# Wake Source Configuration Specification

## Purpose

Define configurable wake sources for the power manager to support flexible deployment scenarios.

## Requirements

### FR-1: Wake Source Bitmask Configuration

The system SHALL provide a `sdf_power_wake_config_t` struct containing:
- `enabled_sources`: Bitmask of `sdf_wake_source_t` values
- `checkin_interval_ms`: Timer wake interval in milliseconds
- `finger_wake_debounce_ms`: GPIO debounce time for fingerprint wake
- `enable_ble_wake`: Allow BLE activity to wake device
- `enable_zigbee_wake`: Allow Zigbee RX to wake device
- `deep_sleep_min_duration_ms`: Minimum duration before deep sleep

### FR-2: Wake Source Types

The system SHALL support the following wake sources:

| Source | Value | Description |
|--------|-------|-------------|
| TIMER | 0x01 | Zigbee check-in timer |
| FINGERPRINT_GPIO | 0x02 | Fingerprint sensor WAKE pin |
| USB | 0x04 | USB connection detect |
| BLE_ACTIVITY | 0x08 | BLE connection/event |
| ZIGBEE_RX | 0x10 | Zigbee incoming command |
| BUTTON | 0x20 | Enrollment button |

### FR-3: Wake Configuration API

```c
esp_err_t sdf_power_set_wake_config(const sdf_power_wake_config_t *config);
esp_err_t sdf_power_get_wake_config(sdf_power_wake_config_t *config);
```

The system SHALL validate that `checkin_interval_ms` is within [1000, 600000] ms.
The system SHALL validate that `deep_sleep_min_duration_ms` is >= 0.

### FR-4: Deep Sleep Entry Criteria

The system SHALL only enter deep sleep when:
1. `deep_sleep_min_duration_ms` has elapsed since last activity, AND
2. Current check-in interval >= `deep_sleep_min_duration_ms`

If criteria not met, system SHALL use light sleep instead.