# Proposal: Enhance Deep Sleep Architecture

## Summary

Extend the power management system with configurable wake sources, RTC retention memory for fast resume, staged wake sequence (BLE radio → sensor → Zigbee), and deep sleep entry criteria. Replace the current binary light/deep sleep with a configurable multi-stage sleep architecture.

## Problem

Current power management (`sdf_power_task`) has limitations:

1. **Binary sleep choice**: Only light sleep (timer + GPIO) or deep sleep fallback (Zigbee not joined)
2. **No wake source prioritization**: Fingerprint GPIO and timer have equal priority
3. **No retention memory**: Full re-initialization on every wake (~200ms)
4. **BLE radio gating all-or-nothing**: No staged wake for lock action vs. check-in
5. **No wake reason propagation**: Components don't know why they woke
6. **Fixed check-in interval**: Can't adapt based on battery/usage

## Solution

### 1. Configurable Wake Sources

```c
// In sdf_power.h
typedef enum {
  SDF_WAKE_SRC_TIMER = 0x01,           // Zigbee check-in timer
  SDF_WAKE_SRC_FINGERPRINT_GPIO = 0x02, // Fingerprint sensor WAKE pin
  SDF_WAKE_SRC_USB = 0x04,              // USB connection
  SDF_WAKE_SRC_BLE_ACTIVITY = 0x08,     // BLE connection/event
  SDF_WAKE_SRC_ZIGBEE_RX = 0x10,        // Zigbee incoming (via radio IRQ)
  SDF_WAKE_SRC_BUTTON = 0x20,           // Enrollment button
} sdf_wake_source_t;

typedef struct {
  uint32_t enabled_sources;             // Bitmask of SDF_WAKE_SRC_*
  uint32_t checkin_interval_ms;         // Timer wake interval
  uint32_t finger_wake_debounce_ms;     // GPIO debounce
  bool enable_ble_wake;                 // Allow BLE to wake
  bool enable_zigbee_wake;              // Allow Zigbee to wake
  uint32_t deep_sleep_min_duration_ms;  // Minimum deep sleep time
} sdf_power_wake_config_t;
```

### 2. RTC Retention Memory

```c
// In sdf_power.h
typedef struct {
  // Fast resume state (survives deep sleep)
  uint32_t magic;                       // Validation marker
  uint8_t ble_transport_state;          // Connected/disconnected
  uint8_t zigbee_join_state;            // Joined/not joined
  uint8_t sensor_power_state;           // On/off
  uint8_t enrolled_user_count;          // Cache
  uint32_t failed_attempts;             // Security state
  int64_t last_activity_us;             // For idle calculation
  int64_t next_checkin_us;              // For timer alignment
  uint16_t crc16;                       // Integrity check
} sdf_power_retention_t;

// API
esp_err_t sdf_power_save_retention(const sdf_power_retention_t *state);
esp_err_t sdf_power_load_retention(sdf_power_retention_t *state);
bool sdf_power_retention_valid(void);
```

### 3. Staged Wake Sequence

```
DEEP SLEEP
    │
    ▼
WAKE (GPIO/Timer) ──▶ RTC Restore (2-5ms)
    │
    ├──▶ STAGE 1: Critical (always)
    │       • Restore NVS security context
    │       • Re-enable interrupts
    │       • Start TWDT
    │
    ├──▶ STAGE 2: Fast Path (BLE lock action)
    │       • If wake_src == FINGERPRINT || BLE_ACTIVITY
    │       • Power on fingerprint sensor
    │       • Enable BLE radio
    │       • Skip Zigbee stack init
    │
    ├──▶ STAGE 3: Full Path (Zigbee check-in)
    │       • If wake_src == TIMER || ZIGBEE_RX
    │       • Init Zigbee stack
    │       • Process pending commands
    │       • Update attributes
    │
    └──▶ STAGE 4: Housekeeping
            • Battery ADC sample
            • Sensor calibration check
            • NVS sync if dirty
```

### 4. Adaptive Check-in Interval

```c
// Dynamic interval based on battery and usage
uint32_t sdf_power_calculate_checkin_interval(void) {
  uint8_t battery = sdf_power_get_battery_percent();
  uint32_t base = config.checkin_interval_ms;  // 15s default
  
  if (battery < 20) return base * 4;      // 60s - critical
  if (battery < 40) return base * 2;      // 30s - low
  if (battery < 60) return base * 1.5;    // 22s - medium
  return base;                             // 15s - normal
}
```

### 5. Wake Reason Events

```c
// Emitted on every wake
typedef struct {
  sdf_wake_source_t source;
  uint32_t sleep_duration_ms;
  bool retention_valid;
  uint8_t battery_percent;
} sdf_power_wake_event_t;

// Event type: SDF_EVT_POWER_WAKE
```

## Architecture Changes

### Modified Components

| Component | Changes |
|-----------|---------|
| `sdf_power` | New wake config API, retention memory, staged wake, adaptive interval |
| `sdf_platform` | Extended sleep API for wake source config, retention read/write |
| `sdf_services` | Subscribe to POWER_WAKE, handle staged resume |
| `sdf_protocol_ble` | BLE radio wake support, connection state in retention |
| `sdf_protocol_zigbee` | Zigbee wake support, fast rejoin from retention |

### New APIs

```c
// sdf_power.h
esp_err_t sdf_power_set_wake_config(const sdf_power_wake_config_t *config);
esp_err_t sdf_power_get_wake_config(sdf_power_wake_config_t *config);
esp_err_t sdf_power_prepare_deep_sleep(void);  // Called before sleep
esp_err_t sdf_power_resume_from_deep_sleep(sdf_wake_source_t *src);

// sdf_platform_sleep.h
esp_err_t sdf_platform_sleep_configure_wake_sources(uint32_t sources, 
                                                     const sdf_power_wake_config_t *config);
esp_err_t sdf_platform_sleep_retention_write(const void *data, size_t len);
esp_err_t sdf_platform_sleep_retention_read(void *data, size_t len);
```

## Configuration (Kconfig)

```kconfig
menu "SDF Power Manager - Deep Sleep"

config SDF_POWER_ENABLE_DEEP_SLEEP
    bool "Enable deep sleep (vs light sleep only)"
    default y

config SDF_POWER_WAKE_SOURCE_TIMER
    bool "Timer wake source (Zigbee check-in)"
    default y

config SDF_POWER_WAKE_SOURCE_FINGERPRINT
    bool "Fingerprint GPIO wake source"
    default y

config SDF_POWER_WAKE_SOURCE_USB
    bool "USB connection wake source"
    default y

config SDF_POWER_WAKE_SOURCE_BLE
    bool "BLE activity wake source (experimental)"
    default n

config SDF_POWER_RETENTION_SIZE
    int "RTC retention memory size (bytes)"
    range 64 4096
    default 256

config SDF_POWER_ADAPTIVE_CHECKIN
    bool "Adaptive check-in interval based on battery"
    default y

config SDF_POWER_STAGED_WAKE
    bool "Enable staged wake sequence"
    default y

endmenu
```

## Benefits

1. **Faster unlock**: Fingerprint wake → BLE action in ~50ms (vs 200ms full init)
2. **Battery life**: Adaptive check-in extends battery 20-40% at low charge
3. **Reliability**: Retention memory preserves security state across power loss
4. **Flexibility**: Configurable wake sources for different deployment scenarios
5. **Observability**: Wake reason events enable diagnostics

## Migration Plan

1. Add retention memory struct and platform APIs
2. Extend `sdf_power` with wake config and staged wake
3. Update `sdf_services` to handle staged resume events
4. Update BLE/Zigbee for fast rejoin
5. Add Kconfig options
6. Update documentation (sdf_sas.md sections 8, 11, 12)

## Acceptance Criteria

- [ ] Deep sleep with fingerprint wake completes unlock in < 100ms
- [ ] Retention memory survives 24h deep sleep
- [ ] Adaptive check-in reduces interval at low battery
- [ ] All wake sources emit POWER_WAKE events with correct source
- [ ] Zigbee fast rejoin works from retention state
- [ ] BLE connection resumes from retention state
- [ ] Power consumption < 10µA in deep sleep
- [ ] Documentation updated per AGENTS.md