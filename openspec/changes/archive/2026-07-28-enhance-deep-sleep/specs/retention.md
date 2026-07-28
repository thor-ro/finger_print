# RTC Retention Memory Specification

## Purpose

Preserve critical state across deep sleep cycles to enable fast resume for unlock operations.

## Requirements

### FR-1: Retention Memory Structure

The system SHALL define `sdf_power_retention_t` in `sdf_power.h`:

```c
typedef struct {
    uint32_t magic;               // Must be 0x5FDEC3A1
    uint8_t ble_transport_state;  // 0=disconnected, 1=connected
    uint8_t zigbee_join_state;    // 0=not joined, 1=joined
    uint8_t sensor_power_state;   // 0=off, 1=on
    uint8_t enrolled_user_count;  // Cached user count
    uint32_t failed_attempts;     // Security counter
    int64_t last_activity_us;     // Last activity timestamp
    int64_t next_checkin_us;      // Next scheduled check-in
    uint16_t crc16;               // CRC16-CCITT over struct (excluding this field)
} sdf_power_retention_t;
```

### FR-2: Retention Memory API

The system SHALL provide the following APIs:

```c
esp_err_t sdf_power_save_retention(const sdf_power_retention_t *state);
esp_err_t sdf_power_load_retention(sdf_power_retention_t *state);
bool sdf_power_retention_valid(void);
```

### FR-3: Retention Memory Size

The system SHALL allocate `SDF_POWER_RETENTION_SIZE` bytes (default 256) in RTC slow memory.

### FR-4: Data Integrity

The system SHALL compute CRC16-CCITT over the retention struct (excluding CRC field) before saving.
The system SHALL verify CRC on load and return invalid if corrupted.
The system SHALL use magic value 0x5FDEC3A1 to detect uninitialized memory.

### FR-5: Retention Memory Contents

The system SHALL save retention memory:
- Before entering deep sleep
- After wake guard expires and during idle

The system SHALL load retention memory:
- Early in `sdf_power_resume_from_deep_sleep()`
- Before staged wake handlers execute