## Context

BLE advertising is stateless — a timer is needed to switch from fast to slow mode. NimBLE does not provide built-in advertising duration management; this must be implemented at the application level using a FreeRTOS timer or esp_timer.

## Goals / Non-Goals

**Goals:**
- Fast advertising (30–60ms) for 30 seconds on start and on each disconnect
- Slow advertising (1280ms interval) after 30 seconds if no connection established

**Non-Goals:**
- Advertising only when a button is pressed (would reduce discoverability)
- Bluetooth Low Energy extended advertising

## Decisions

**Use esp_timer for the fast→slow switchover.**

Add a static `esp_timer_handle_t s_adv_switch_timer`. On advertising start, arm it for 30 seconds. On timer fire, call `ble_gap_adv_stop()` followed by `ble_gap_adv_start()` with slow interval params. On disconnect, cancel the timer and restart advertising at fast interval (re-arming the 30s timer).

```c
// Fast params (current):
.itvl_min = BLE_GAP_ADV_FAST_INTERVAL1_MIN,  // 30ms
.itvl_max = BLE_GAP_ADV_FAST_INTERVAL1_MAX,  // 60ms

// Slow params (new):
.itvl_min = BLE_GAP_ADV_SLOW_INTERVAL1_MIN,  // 1280ms
.itvl_max = BLE_GAP_ADV_SLOW_INTERVAL1_MAX,  // 2560ms
```

## Risks / Trade-offs

- [Timer context] `esp_timer` callbacks run in a dedicated high-priority task. The BLE API calls inside the callback (`ble_gap_adv_stop/start`) are safe to call from non-NimBLE tasks per ESP-IDF NimBLE docs.
- [Discoverability] After 30 seconds without connection, scanning apps may not detect the device quickly. A 1280ms interval means ~1-2 seconds latency for discovery — acceptable for a companion app.
