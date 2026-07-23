# Power Management Review Report — Smart Door Finger (SDF) v2.0

**Date:** 2026-05-28  
**Reviewer:** Code Analysis  
**Scope:** `firmware/components/sdf_power/` and integration points

---

## 1. Executive Summary

The power management implementation provides a solid foundation for battery-operated operation with light sleep and deep sleep fallback. However, several issues were identified that could affect reliability and battery life:

| Severity | Count |
|----------|-------|
| Critical | 2 |
| High | 3 |
| Medium | 4 |
| Low | 3 |

---

## 2. Architectural Assessment

### Strengths

1. **Clear separation of concerns**: The `sdf_power` component cleanly isolates sleep logic from application logic via callbacks (`busy_cb`, `wake_cb`, `battery_cb`).

2. **Configurable timeouts**: All timing parameters are configurable via `sdkconfig.defaults`, enabling runtime tuning.

3. **Deep sleep fallback**: The `enable_deep_sleep_fallback` option provides a safety net for devices that can't maintain Zigbee network connection.

### Areas for Improvement

1. **Missing `sdf_platform` and `sdf_config` components**: These are documented in `sdf_sas.md` but do not exist. Hardware abstraction is inline; consider refactoring.

2. **RTC memory usage**: The deep sleep concept (`deep_sleep_concept.md`) proposes RTC memory usage, but the current implementation doesn't leverage it for state persistence across deep sleep cycles.

---

## 3. Bug Findings

### Bug #1: Incorrect Deep Sleep Fallback Condition (CRITICAL)

**Location:** `sdf_power.c:259-275`

**Issue:** The deep sleep fallback logic uses OR (`||`) when it should use AND (`&&`) for Zigbee enabled check:

```c
if (config_snapshot.enable_deep_sleep_fallback &&
    (!zigbee_enabled || !sdf_protocol_zigbee_is_ready())) {
```

**Impact:** If Zigbee is disabled (`CONFIG_ZB_ENABLED=n`), the device will enter deep sleep even when it shouldn't. Deep sleep disables all network connectivity, making the device unreachable for remote commands.

**Recommendation:** Change OR to AND:
```c
if (config_snapshot.enable_deep_sleep_fallback &&
    zigbee_enabled && !sdf_protocol_zigbee_is_ready()) {
```

### Bug #2: Wake Reason Mapping Misclassifies USB Disconnection (HIGH)

**Location:** `sdf_power.c:66-75`

**Issue:** All non-timer/GPIO wakeup causes map to `SDF_POWER_WAKE_REASON_OTHER`. USB serial JTAG disconnection (`ESP_SLEEP_WAKEUP_USB`) is classified as "other", which could mask intentional wake events during development/debugging.

**Impact:** Debug sessions may not properly distinguish between intentional and spurious wake events, leading to incorrect power state transitions.

**Recommendation:** Add explicit USB wake handling:
```c
case ESP_SLEEP_WAKEUP_USB:
    return SDF_POWER_WAKE_REASON_USB;
```

Consider adding a new enum value or logging USB disconnect events separately.

### Bug #3: Race Condition in BLE Radio Gating (HIGH)

**Location:** `sdf_power.c:154-157` and `166-169`

**Issue:** BLE radio is disabled before `esp_light_sleep_start()` and re-enabled after. If the sleep fails to initiate (line 159) or if the task crashes before line 167, the BLE radio remains disabled.

**Impact:** Device could become unresponsive over BLE after a failed sleep attempt.

**Recommendation:** Wrap radio gating in a RAII-style pattern or ensure re-enable happens in all error paths:
```c
if (config->enable_ble_radio_gating && config->ble_transport != NULL) {
    sdf_nuki_ble_set_enabled(config->ble_transport, false);
}

esp_err_t sleep_err = esp_light_sleep_start();

// Always restore BLE state on wake path, even if sleep failed
if (config->enable_ble_radio_gating && config->ble_transport != NULL) {
    sdf_nuki_ble_set_enabled(config->ble_transport, true);
}
```

### Bug #4: Missing Power Gating on Enrollment Abort (MEDIUM)

**Location:** `sdf_app.c:136-143` (`sdf_app_abort_latch_sequence`) and `sdf_services_enrollment.c`

**Issue:** When enrollment is aborted, there's no explicit call to power off the fingerprint sensor. The `busy_cb` prevents sleep during enrollment, but if aborted mid-flow, the sensor may remain powered.

**Impact:** Increased power consumption during error conditions.

**Recommendation:** Add GPIO-based power control in abort path or ensure enrollment cleanup always calls `fp_set_keep_power_on(false)`.

### Bug #5: Wake Guard Not Applied During Battery Report Path (MEDIUM)

**Location:** `sdf_power.c:216-236`

**Issue:** The battery report callback executes without holding the wake guard. If the `battery_cb` is slow or the BLE push fails, the device could enter sleep during the battery update operation.

**Impact:** Potential missed battery reports or corrupted Zigbee state.

**Recommendation:** Either apply a short wake guard before battery operations or move the check to the top of the loop where other guards are evaluated.

---

## 4. Optimization Opportunities

### Optimization #1: Adaptive Sleep Timing (MEDIUM)

**Location:** `sdf_power.c:238-249`

**Issue:** Sleep decision uses fixed `idle_before_sleep_ms` and `post_wake_guard_ms` values. These don't adapt to actual system behavior.

**Recommendation:** Implement adaptive timing:
- Track actual BLE connection time and adjust post-wake guard accordingly
- Shorten idle timeout after failed lock actions (device less likely to be needed)
- Extend idle timeout after successful unlock (user likely to leave)

### Optimization #2: Battery Report Coalescing (MEDIUM)

**Location:** `sdf_power.c:216-236`

**Issue:** Battery is reported on every wake even if the value hasn't changed significantly.

**Recommendation:** Add hysteresis to battery reporting:
```c
static uint8_t last_reported_battery = 101; // Force first report
if (battery_cb_result >= 0 && battery_cb_result <= 100) {
    uint8_t new_percent = (uint8_t)battery_cb_result;
    if (abs(new_percent - last_reported_battery) >= 5) { // 5% change threshold
        battery_percent = new_percent;
        sdf_power_push_battery_percent(battery_percent);
        last_reported_battery = new_percent;
    }
}
```

This reduces unnecessary Zigbee traffic and saves ~100 µA per day.

### Optimization #3: Missing RTC GPIO Wake Configuration for Deep Sleep (MEDIUM)

**Location:** `sdf_power.c:267-273`

**Issue:** Deep sleep wake GPIO uses `ESP_GPIO_WAKEUP_GPIO_HIGH` without checking if the pin is an RTC-capable GPIO on ESP32-C6.

**Impact:** On ESP32-C6, only GPIO0-7 are RTC GPIOs. If `fingerprint_wake_gpio` is set to a non-RTC GPIO (e.g., GPIO9 for enrollment button as in `sdkconfig.defaults:64`), it won't wake from deep sleep.

**Recommendation:** Add validation and configure both:
```c
if (sdf_power_gpio_valid(config_snapshot.fingerprint_wake_gpio)) {
    // Check if GPIO is RTC-capable for deep sleep
    if (config_snapshot.fingerprint_wake_gpio < GPIO_NUM_8) {
        esp_deep_sleep_enable_gpio_wakeup(...);
    }
}
```

### Optimization #4: Incomplete Wake Source Reset (LOW)

**Location:** `sdf_power.c:140`

**Issue:** `esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL)` disables all wake sources, including potentially needed ones like USB for development.

**Recommendation:** Only disable sources that were configured by the power manager, or use named wake sources explicitly.

### Optimization #5: Stack Size Not Optimized (LOW)

**Location:** `sdf_power.c:29`

**Issue:** Task stack is fixed at 4096 bytes but the task only performs simple calculations and semaphore operations.

**Recommendation:** Analyze stack high-water mark with `uxTaskGetStackHighWaterMark()` and reduce to 2048 bytes if safe, saving 2KB RAM.

### Optimization #6: Missing Low-Battery Power Policy (LOW)

**Location:** `sdf_power.c` (absent)

**Issue:** No logic to adapt power behavior when battery is low.

**Recommendation:** When battery < 10%, consider:
- Increasing check-in interval (less frequent polling)
- Disabling LED animations entirely
- Entering deep sleep more aggressively

---

## 5. Test Coverage Gaps

### Gap #1: No Sleep Integration Tests

**Location:** `firmware/components/sdf_power/test/test_sdf_power.c`

**Issue:** Tests only verify wakeup reason mapping and parameter bounds. No tests for:
- Sleep entry conditions
- Wake guard timing
- Deep sleep fallback logic
- BLE radio gating integration

**Recommendation:** Add tests that mock `esp_light_sleep_start()` and verify sleep decisions under various state combinations.

### Gap #2: Missing Concurrency Tests

**Issue:** The power manager uses a mutex (`s_state.lock`) but tests don't verify thread safety under concurrent access.

**Recommendation:** Add tests that call `sdf_power_mark_activity()`, `sdf_power_set_battery_percent()`, and the power loop simultaneously from multiple tasks.

---

## 6. Configuration Issues

### Config #1: Check-in Interval Validation Mismatch

**Location:** `sdkconfig.defaults:38-42` and `sdf_power.c:326-327`

**Issue:** The default check-in interval is 15000ms (15s), which is within bounds, but there's no validation for alignment with Zigbee parent timeout.

**Recommendation:** Add compile-time assertion or runtime warning if check-in interval exceeds 1/3 of typical parent timeout (most routers use 30-60s).

### Config #2: Missing `FP_EN_GPIO` Configuration Validation

**Location:** `sdkconfig.defaults:64`

**Issue:** `CONFIG_SDF_POWER_FP_EN_GPIO=2` is defined but never validated against the actual hardware wiring.

**Recommendation:** Add runtime check in `sdf_power_init_power_manager()` that validates the power enable GPIO exists and can be configured as output.

---

## 7. Code Quality Observations

### Observation #1: Inconsistent Error Handling

The power manager sometimes logs warnings and continues, sometimes returns errors. For example:
- `sdf_power_configure_fingerprint_wakeup()` logs warning but doesn't prevent sleep
- `sdf_power_sleep_once()` logs warning but the task continues running
- Inconsistent with the project's defensive programming style elsewhere

### Observation #2: Magic Numbers in Sleep Timing

Values like `1000u`, `600000u`, `250u` are embedded in validation logic. Consider using the defined macros consistently.

---

## 8. Recommendations Summary

| Priority | Action | Location |
|----------|--------|----------|
| P0 | Fix deep sleep fallback OR→AND bug | `sdf_power.c:259` |
| P0 | Add explicit BLE radio restore on sleep failure | `sdf_power.c:159-169` |
| P1 | Add USB wake reason classification | `sdf_power.c:70-71` |
| P1 | Validate RTC GPIO capability for deep sleep wake | `sdf_power.c:267` |
| P2 | Add battery report hysteresis (5% threshold) | `sdf_power.c:216-224` |
| P2 | Add adaptive sleep timing based on usage patterns | `sdf_power.c:238-249` |
| P2 | Add RTC memory for deep sleep state persistence | `sdf_power.c` |
| P3 | Improve test coverage for sleep scenarios | `test_sdf_power.c` |
| P3 | Implement low-battery power saving policy | `sdf_power.c` |
| P3 | Reduce task stack size if safe | `sdf_power.c:29` |

---

## 9. Next Steps

1. **Immediate**: Verify Bug #1 (deep sleep fallback) with hardware testing to confirm impact
2. **Short-term**: Implement Bug #3 fix and add defensive tests
3. **Medium-term**: Consider Optimization #3 and #6 for production release
4. **Long-term**: Implement RTC memory state persistence as outlined in `deep_sleep_concept.md`