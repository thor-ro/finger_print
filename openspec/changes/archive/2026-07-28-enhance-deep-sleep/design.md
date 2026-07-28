# Design: Enhanced Deep Sleep Architecture

## Context

The current power management (`sdf_power_task`) uses a binary approach: light sleep with timer/GPIO wake, or deep sleep fallback when Zigbee is not joined. No state is preserved across deep sleep, requiring full re-initialization (~200ms). Wake sources have no prioritization or configurability, and components cannot determine wake reason.

## Goals / Non-Goals

**Goals:**
- Configurable wake source selection via Kconfig/API
- RTC slow memory retention for fast resume
- Staged wake sequence: critical → fast path (BLE lock) → full path (Zigbee)
- Adaptive check-in interval based on battery level
- Wake reason propagation via `SDF_EVENT_ROUTER_POWER_WAKE`

**Non-Goals:**
- Wake source prioritization (all enabled sources wake)
- Dynamic wake configuration at runtime (configurable but static after init)

## Decisions

### Decision 1: Wake Configuration via Structured API

**Chosen:** `sdf_power_wake_config_t` struct with bitmask of enabled sources

**Rationale:** Provides clear, type-safe configuration while allowing future extension. Bitmask allows O(1) checks for enabled sources.

**Alternatives Considered:**
- Individual Kconfig options (harder to validate combinations)
- Per-source enable API (more function calls)

### Decision 2: RTC Slow Memory for Retention

**Chosen:** Use ESP-IDF RTC slow memory (`RTC_SLOW_MEM`) with CRC validation

**Rationale:** RTC slow memory retains data through deep sleep on ESP32-C6. CRC16 provides integrity check against corruption.

**Alternatives Considered:**
- NVS storage (slower, wear on flash)
- No retention (simpler but slower resume)

### Decision 3: Staged Wake Control Flow

**Chosen:** Wake event carries source; handlers branch on source type

**Rationale:** Single wake event type keeps interfaces simple. Components subscribe to `POWER_WAKE` and check source to determine action.

**Alternatives Considered:**
- Separate event types per wake source (more complexity)
- Dedicated callback registration per source (more API surface)

### Decision 4: Adaptive Check-in Calculation

**Chosen:** Battery-based scaling in `sdf_power_calculate_checkin_interval()`

**Rationale:** Simple threshold logic with clear latency vs. power trade-off. Called before each sleep entry.

**Alternatives Considered:**
- Moving average of activity (complex, needs more retention state)
- Configurable curves (overcomplicated)

### Decision 5: Deep Sleep Entry Criteria

**Chosen:** Configurable minimum duration to prevent thrashing

**Rationale:** Avoid entering deep sleep for short idle periods where light sleep overhead isn't worth it.

## Risks / Trade-offs

| Risk | Mitigation |
|------|------------|
| RTC memory corruption | CRC16 validation, fallback to clean state |
| Deep sleep < 10µA target | Measure on hardware, tune config defaults |
| Wake source misconfig | Validate bitmask at init, log warnings |
| BLE reconnect race on resume | Guard time after wake before allowing sleep |

## Migration Plan

1. Extend `sdf_platform_sleep.h` with retention APIs and wake config
2. Add `sdf_power_wake_config_t` and retention struct to `sdf_power.h`
3. Modify `sdf_power_task` to apply staged wake and retention
4. Add Kconfig options to `sdf_power/Kconfig`
5. Update `sdf_services` to handle staged resume
6. Test on hardware: retention survive, wake sources, power consumption