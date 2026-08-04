## Context

The OTA task calls WiFi and HTTP APIs and runs without any feedback to the BLE client. The notify API (`sdf_ble_companion_notify_ota`) already exists but is never called during the task. A challenge is that the OTA task only has access to the request struct, not the conn_handle — the companion module needs a way to broadcast to all authenticated connections.

## Goals / Non-Goals

**Goals:**
- Send OTA state updates via `sdf_ble_companion_notify_ota()` at key stages
- Add a helper in `sdf_ble_companion.c` to broadcast a notification to all authenticated connections
- Update the web companion to subscribe and display progress

**Non-Goals:**
- Per-byte progress reporting (too chatty over BLE)
- OTA abort/cancel from the companion

## Decisions

**Wire format: JSON status messages.**
```json
{"status": "wifi_connecting"}
{"status": "wifi_connected"}
{"status": "downloading", "progress": 45}
{"status": "verifying"}
{"status": "success"}
{"status": "failed", "error": "wifi_timeout"}
```

**Broadcast helper:** Add `sdf_ble_companion_broadcast_ota(const uint8_t *data, size_t len)` that iterates `s_connections[]` and calls `notify_ota()` for each authenticated connection. The OTA task calls this helper.

**Progress reporting:** Report at 0%, 25%, 50%, 75%, 100% to avoid flooding. Track `last_reported_pct` and only notify when the threshold crosses a 25% boundary.

## Risks / Trade-offs

- [Task context] `sdf_ble_companion_broadcast_ota()` will be called from the `sdf_ble_ota` task, not the NimBLE task. This is fine — `ble_gatts_notify_custom()` is thread-safe per ESP-IDF NimBLE docs.
- [Connection lost during OTA] If BLE disconnects mid-OTA, the notify calls will return errors but the OTA continues — correct behavior.
