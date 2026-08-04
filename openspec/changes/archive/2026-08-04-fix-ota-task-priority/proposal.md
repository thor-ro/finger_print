## Why

The BLE OTA task is spawned at `tskIDLE_PRIORITY + 2` (priority 2). ESP-IDF's lwIP stack and WiFi event handlers run at priority 5 and higher. The OTA task, which calls `esp_wifi_connect()` and `esp_http_client_read()`, will be preempted and may starve, causing WiFi connection timeouts or download stalls. ESP-IDF OTA examples use priority 5–10 for the download task.

## What Changes

- Raise `sdf_ble_ota_task` priority from `tskIDLE_PRIORITY + 2` to `tskIDLE_PRIORITY + 8` (priority 8)
- This matches typical ESP-IDF OTA task priority examples and stays below the NimBLE host task (priority 9) to avoid starving BLE during OTA

## Capabilities

### New Capabilities
- None

### Modified Capabilities
- None

## Impact

- `firmware/components/sdf_ble_companion/src/sdf_ble_companion_ota.c` — one-line change to `xTaskCreate` priority argument
