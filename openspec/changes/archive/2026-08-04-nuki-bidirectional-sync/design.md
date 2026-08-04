## Context

Polling the Nuki lock state requires initiating a BLE connection to the lock (not the companion service). The SDF already has `sdf_app_request_keyturner_state()` for this purpose — it's called on wake and after completing a lock action. The gap is that it's not called periodically when idle.

## Goals / Non-Goals

**Goals:**
- Poll keyturner state at a configurable interval (default: Zigbee check-in interval) when idle and BLE is ready
- Detect externally-initiated lock state changes and propagate them to Zigbee

**Non-Goals:**
- Nuki push notifications via the Nuki BLE notification mechanism (requires dedicated connection listener — too complex for v1)
- Real-time detection (polling introduces latency equal to the poll interval)

## Decisions

**Extend the power wake timer path.** The power manager already wakes periodically at `checkin_interval_ms`. The `sdf_app_power_wakeup()` callback already calls `sdf_app_request_keyturner_state()` when waking due to timer. This is the correct hook — no new timer needed.

**Gap:** `sdf_app_power_wakeup()` only requests state if `sdf_nuki_ble_is_ready(&s_ble)` — it skips if BLE is not connected. When `ble_connect_on_demand` is true, BLE is disconnected when idle, so the poll never happens.

**Fix:** In the timer-wake path, if BLE is not ready, call `sdf_app_resume_ble_transport("periodic state poll")` to initiate connection, then request keyturner state on `on_ble_ready`.

Add a flag `s_periodic_poll_pending` so that `on_ble_ready` knows to request keyturner state if the connection was for polling (not for a lock action).

## Risks / Trade-offs

- [Power cost] Each poll wakes the BLE radio, connects to Nuki (several seconds), requests state, and disconnects. At the default 15s check-in interval this is frequent. Consider making poll interval configurable independently from check-in interval.
- [Nuki response latency] Nuki may be slow to respond if it's in low-power mode. The existing BLE transport timeout handles this.
