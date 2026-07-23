# Power Management Review

**Date:** 2026-05-28
**Scope:** `sdf_power` component, `battery.c` driver, power integration in `sdf_app.c`, Zigbee SED sleep, BLE radio gating

---

## 1. Architecture Summary

The power management operates at three cooperating layers:

```
┌─────────────────────────────────────────────────────┐
│  Application Layer (sdf_power task)                 │
│  - Explicit sleep decisions based on idle time,     │
│    busy callbacks, wake guards, USB connection       │
│  - BLE radio gating before sleep                    │
│  - Battery reporting over Zigbee                    │
│  - Deep sleep fallback for unjoined Zigbee          │
├─────────────────────────────────────────────────────┤
│  Zigbee SED Layer (esp_zb_sleep_enable/now)         │
│  - Autonomous sleep between check-in polls          │
│  - CAN_SLEEP signal triggers immediate sleep        │
├─────────────────────────────────────────────────────┤
│  ESP-IDF PM Layer (CONFIG_PM_ENABLE)                │
│  - CPU frequency scaling (DFS)                      │
│  - FreeRTOS tickless idle                           │
│  - Automatic idle sleep at RTOS level               │
└─────────────────────────────────────────────────────┘
```

**Data flow:** `sdf_power` task wakes every `loop_interval_ms` (250ms default) -> snapshots state under mutex -> checks battery report schedule -> evaluates `allow_sleep` (5 conditions) -> enters `esp_light_sleep_start()` with BLE gated -> on wake, restores BLE, maps reason, notifies app callback.

**Key design decisions:**
- Dedicated FreeRTOS task (priority 4, 4096B stack) owns all sleep decisions
- BLE radio is explicitly disabled before light sleep and re-enabled after (radio gating)
- Wake guard prevents rapid sleep-wake oscillation (1500ms default)
- Deep sleep used as fallback when Zigbee stack fails to join network
- Battery reporting is decoupled from sleep; pushed periodically over Zigbee Power Config cluster

---

## 2. Found Bugs

### BUG-1: Battery report uses stale value under race condition
**File:** `sdf_power.c:224-248`
**Severity:** Medium

The battery callback is invoked at line 227-228, but the local `battery_percent` used for the Zigbee push at line 248 is the snapshot taken at line 212. If `sdf_power_set_battery_percent()` is called by another thread between lines 232 and 248, the pushed value is stale. Additionally, the battery push at line 248 happens *after* the mutex is released at line 246, so there is a TOCTOU window where `battery_percent` local can diverge from `s_state.battery_percent`.

**Fix:** Move `sdf_power_push_battery_percent(battery_percent)` inside the critical section, or re-read `battery_percent` from `s_state` after releasing the lock:

```c
// Option: push inside the critical section
if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(SDF_POWER_LOCK_WAIT_MS)) == pdTRUE) {
    s_state.battery_percent = battery_percent;
    s_state.next_battery_report_us = now_us + ...;
    s_state.wake_guard_until_us = esp_timer_get_time() + ...;
    sdf_power_push_battery_percent(battery_percent);  // push while holding lock
    xSemaphoreGive(s_state.lock);
}
```

### BUG-2: Deep sleep bypasses wake guard and post-wake state update
**File:** `sdf_power.c:270-285`
**Severity:** High

When `enable_deep_sleep_fallback` triggers, `esp_deep_sleep_start()` is called at line 284. This is a *noreturn* call -- the MCU restarts. However, the code after it (`sdf_power_sleep_once` at line 287 and the wake guard update at lines 288-295) is unreachable dead code. This is not a bug per se, but:

1. The fingerprint GPIO wakeup for deep sleep (line 279-281) uses `esp_deep_sleep_enable_gpio_wakeup()` which takes a bitmask, but the timer wakeup is never configured for deep sleep. On wake from deep sleep, there is no timer -- only GPIO. If the GPIO fails to trigger, the device is permanently in deep sleep until manual reset.
2. After deep sleep wake, the system fully reinitializes. The `sdf_power` task is recreated, but there is no logging or diagnostic to distinguish a deep sleep wake from a cold boot.

**Fix:** Add a timer wakeup for deep sleep as well, and log the deep sleep wake cause:

```c
if (config_snapshot.enable_deep_sleep_fallback &&
    zigbee_enabled && !sdf_protocol_zigbee_is_ready()) {
    ESP_LOGI(TAG, "Entering deep sleep (Zigbee not joined)");
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    // Add timer wakeup as fallback
    esp_deep_sleep_enable_timer_wakeup(
        (uint64_t)config_snapshot.checkin_interval_ms * 1000ULL);
    if (sdf_power_gpio_valid(config_snapshot.fingerprint_wake_gpio)) {
        esp_deep_sleep_enable_gpio_wakeup(
            1ULL << config_snapshot.fingerprint_wake_gpio,
            ESP_GPIO_WAKEUP_GPIO_HIGH);
    }
    esp_deep_sleep_start(); // noreturn
}
```

### BUG-3: Battery ADC returns 100% on error, masking failures
**File:** `battery.c:71-78`
**Severity:** Medium

When `s_adc_handle` is NULL (line 71-72) or `adc_oneshot_read` fails (line 77-78), the function returns 100. This means a disconnected or broken ADC will always report full battery. The power manager will never trigger low battery warnings, and Zigbee will report 100% indefinitely.

**Fix:** Return -1 on error (the power manager already handles `battery_cb_result < 0` at line 231 by not updating):

```c
int sdf_drivers_battery_get_percent(void) {
    if (s_adc_handle == NULL) {
        return -1;  // error, not 100
    }
    // ...
    if (err != ESP_OK) {
        return -1;  // error, not 100
    }
    // ...
}
```

### BUG-4: Battery percentage check allows stale value
**File:** `sdf_power.c:231-233`
**Severity:** Low

```c
if (battery_cb_result >= 0 && battery_cb_result <= 100) {
    battery_percent = (uint8_t)battery_cb_result;
}
```

If the battery callback returns a value outside 0-100 (e.g., -1 for error, or >100 due to a bug), the local `battery_percent` retains its previous snapshot value. This is silently used for the Zigbee push at line 248. The stale value could be significantly out of date.

**Fix:** Log a warning when the callback returns an invalid value:

```c
if (battery_cb_result >= 0 && battery_cb_result <= 100) {
    battery_percent = (uint8_t)battery_cb_result;
} else {
    ESP_LOGW(TAG, "Battery callback returned invalid value: %d", battery_cb_result);
}
```

### BUG-5: No ADC deinitialization path
**File:** `battery.c`
**Severity:** Low

The ADC handle `s_adc_handle` is never freed. `sdf_drivers_deinit()` (in `sdf_drivers.c`) deinitializes fingerprint and LED but not the battery ADC. This leaks the ADC unit and calibration handle.

**Fix:** Add `sdf_drivers_battery_adc_deinit()` that calls `adc_oneshot_del_unit()` and `adc_cali_delete_scheme()`.

---

## 3. Architectural Issues

### ARCH-1: Dual PM frameworks may conflict
Both ESP-IDF PM (`CONFIG_PM_ENABLE` + tickless idle) and the application `sdf_power` manage sleep. During the `vTaskDelay()` at line 298, ESP-IDF's tickless idle can independently enter light sleep without going through the application's sleep guards. This could cause:
- Unexpected wakeups from non-configured sources
- BLE radio not being gated before sleep (since tickless idle doesn't know about BLE gating)
- Race conditions between the two sleep controllers

**Recommendation:** Either disable `CONFIG_FREERTOS_USE_TICKLESS_IDLE` and rely solely on the application manager, or disable `sdf_power`'s explicit sleep and let ESP-IDF PM handle all sleep decisions (simpler but less control).

### ARCH-2: Tight coupling to BLE transport via include
`sdf_power.h:16` includes `sdf_nuki_ble_transport.h`, creating a hard dependency. The Linux workaround (forward declaration at line 18) is fragile. This coupling exists solely to pass the transport pointer for radio gating.

**Recommendation:** Use an opaque `void *ble_transport` in the config struct and cast internally. Or define a minimal `sdf_power_ble_ops` interface with `enable/disable` function pointers.

### ARCH-3: Non-power GPIO assignments in power Kconfig
`SDF_ENROLLMENT_BTN_GPIO` and `SDF_WS2812_LED_GPIO` are defined in the power Kconfig menu despite being unrelated to power management. This violates single-responsibility and confuses configuration discovery.

**Recommendation:** Move these to a dedicated "Hardware" or "GPIO" Kconfig menu.

### ARCH-4: Hardcoded values not in Kconfig
- `SDF_APP_BATTERY_ADC_GPIO` hardcoded to 0 in `sdf_app.c`
- Low battery threshold (20%) hardcoded in `sdf_app_on_fingerprint_unlock()`
- Battery voltage range (2000-3000mV) hardcoded in `battery.c:93-98`
- Voltage divider multiplier (2x) hardcoded in `battery.c:91`

---

## 4. Optimization Proposals

### OPT-1: ADC multi-sample averaging
**Impact:** Improved battery accuracy
**Effort:** Low

Single-shot ADC reads are noisy. Add a configurable sample count (e.g., 8 samples) with averaging:

```c
#define SDF_BATTERY_ADC_SAMPLES 8

int sdf_drivers_battery_get_percent(void) {
    if (s_adc_handle == NULL) return -1;
    int sum = 0;
    for (int i = 0; i < SDF_BATTERY_ADC_SAMPLES; i++) {
        int raw;
        if (adc_oneshot_read(s_adc_handle, s_adc_channel, &raw) != ESP_OK)
            return -1;
        sum += raw;
    }
    int raw = sum / SDF_BATTERY_ADC_SAMPLES;
    // ... rest of conversion
}
```

### OPT-2: Adaptive loop interval
**Impact:** Reduced power consumption
**Effort:** Medium

The power task loops every 250ms unconditionally. When the system is idle and no sleep is imminent, the loop could back off to a longer interval (e.g., 2-5 seconds), reducing CPU wakeups:

```c
int64_t time_to_sleep = wake_guard_until_us - now_us;
int64_t time_to_battery = next_battery_report_us - now_us;
int64_t next_event = MIN(time_to_sleep, time_to_battery);
int delay_ms = MIN(config_snapshot.loop_interval_ms,
                   (int)(next_event / 1000));
vTaskDelay(pdMS_TO_TICKS(MAX(delay_ms, 50)));
```

### OPT-3: Fingerprint sensor power gating during deep sleep
**Impact:** Reduced quiescent current
**Effort:** Low

The fingerprint sensor's power enable GPIO (`SDF_POWER_FP_EN_GPIO`, GPIO 2) is not explicitly driven low before deep sleep. If the sensor draws current even when idle, powering it off before deep sleep would save power.

### OPT-4: Dynamic check-in interval based on battery
**Impact:** Extended battery life
**Effort:** Medium

When battery is low (<20%), increase the Zigbee check-in interval (e.g., from 15s to 60s) to reduce radio wakeups. When battery is critical (<10%), consider entering a low-power mode that disables non-essential features.

### OPT-5: Reduce power task stack size
**Impact:** RAM savings
**Effort:** Low

The power task uses 4096 bytes (`SDF_POWER_TASK_STACK`). The actual stack usage is likely under 1500 bytes (no deep call chains, no large local arrays). A stack audit with `uxTaskGetStackHighWaterMark()` could confirm this, allowing reduction to 2048 bytes and saving ~2KB RAM.

### OPT-6: Battery report coalescing
**Impact:** Reduced Zigbee traffic
**Effort:** Low

Currently, `sdf_power_set_battery_percent()` (called from `sdf_app.c`) pushes immediately via Zigbee AND the periodic battery report pushes independently. This can result in duplicate pushes. Coalesce by only pushing when the value changes by a meaningful threshold (e.g., 5%).

---

## 5. Test Coverage Assessment

| Area | Coverage | Gap |
|---|---|---|
| Wake reason mapping | 3 tests (unit) | Covers all enum values |
| Checkin interval clamping | 1 test (unit) | Boundary values covered |
| Battery percent bounds | 1 test (unit) | Boundary values covered |
| Actual sleep/wake behavior | None | Requires hardware |
| BLE radio gating | None | Requires BLE stack |
| Battery ADC accuracy | None | Requires hardware |
| Deep sleep fallback | None | Requires Zigbee stack |
| Power task loop logic | None | Requires mocking |
| Wake guard timing | None | Requires time mocking |

**Recommendation:** Add integration-level tests that mock the sleep API and verify the `sdf_power_task` decision logic (allow_sleep evaluation, battery report scheduling, wake guard enforcement).

---

## 6. Summary

| Category | Count |
|---|---|
| Bugs found | 5 |
| Architectural issues | 4 |
| Optimization proposals | 6 |

The power management implementation is well-structured with clear separation between the application decision layer, Zigbee SED sleep, and ESP-IDF PM. The main concerns are: the dual-PM framework conflict (ARCH-1), the battery error masking (BUG-3), and the deep sleep fallback lacking a timer safety net (BUG-2). The code is generally clean and maintainable, with good use of mutex protection and configurable parameters via Kconfig.
