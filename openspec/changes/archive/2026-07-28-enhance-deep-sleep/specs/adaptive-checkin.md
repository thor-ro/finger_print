# Adaptive Check-in Specification

## Purpose

Adjust Zigbee check-in interval based on battery level to extend battery life at low charge.

## Requirements

### FR-1: Adaptive Interval Calculation

The system SHALL implement `sdf_power_calculate_checkin_interval()`:

```c
uint32_t sdf_power_calculate_checkin_interval(void);
```

### FR-2: Battery Thresholds

The system SHALL scale the base interval as follows:

| Battery Level | Scaling Factor | Example (15s base) |
|---------------|----------------|-------------------|
| >= 60% | 1x (base) | 15s |
| 40-59% | 1.5x | 22s |
| 20-39% | 2x | 30s |
| < 20% | 4x | 60s |

### FR-3: Configuration

The system SHALL make adaptive interval configurable via Kconfig:

```
config SDF_POWER_ADAPTIVE_CHECKIN
    bool "Adaptive check-in interval based on battery"
    default y
```

When disabled, the system SHALL use the configured `checkin_interval_ms` without scaling.

### FR-4: Interval Update Timing

The system SHALL recalculate the check-in interval:
- After battery percentage changes
- After wake from deep sleep (if retention valid)
- Before configuring timer wakeup