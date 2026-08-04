## Why

The BLE OTA task runs, downloads, and applies firmware, but never notifies the connected companion app of progress or result. The web companion shows "OTA triggered successfully. Check device status." and then goes silent. The user has no way to know whether the OTA succeeded or failed without physical access to the device.

## What Changes

- Add progress notifications from the OTA task via `sdf_ble_companion_notify_ota()`
- Notify on: WiFi connecting, WiFi connected, download started, download progress (percentage), verify success/failure, apply success/failure
- The web companion shows a progress bar and final success/error message

## Capabilities

### New Capabilities
- None

### Modified Capabilities
- None

## Impact

- `firmware/components/sdf_ble_companion/src/sdf_ble_companion_ota.c` — add notify calls throughout `sdf_ble_ota_task`
- `web-companion/app.js` — subscribe to OTA characteristic notifications, update progress UI
- `web-companion/index.html` — add progress bar element to OTA section
