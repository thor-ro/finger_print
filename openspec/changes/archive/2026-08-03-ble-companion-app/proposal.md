## Why

The Smart Door bridge currently requires a Zigbee coordinator for initial configuration, user enrollment, and OTA updates. This presents a high barrier to entry and a complex setup process. A direct smartphone interface via Web Bluetooth allows users to securely configure the device, enroll fingerprints, and manage updates natively without needing third-party Zigbee hubs.

## What Changes

- Add a new BLE GATT server service (Companion Service) on the ESP32-C6.
- Build a static Web App hosted on GitHub Pages that connects to the ESP32-C6 using the Web Bluetooth API (WebBLE).
- Implement an authentication state machine over BLE GATT (Username + Password Hash).
- Restrict initial Web App registration by requiring a physical "Admin Finger" scan on the device for authorization.
- Support firmware updates by passing Wi-Fi credentials and an HTTPS firmware URL over BLE, prompting the ESP32-C6 to download the OTA via Wi-Fi directly to avoid BLE MTU limits and battery drain.

## Capabilities

### New Capabilities
- `ble-companion-service`: A GATT server handling WebBLE authentication, device configuration, and OTA triggering.
- `web-companion-app`: The GitHub Pages static web app utilizing Web Bluetooth for device management.

### Modified Capabilities
- `sdf-services-tasks`: Must support triggering a live "Admin Auth" scan to authorize web registration.

## Impact

- `sdf_app` / `sdf_protocol_ble`: Will host the Nuki BLE client and Companion GATT server as central and peripheral roles on one shared NimBLE host. `sdf_protocol_ble` owns host lifecycle; the companion registers its GATT service before the host starts.
- `sdf_storage`: NVS schema will expand to store user account hashes.
- `sdf_power`: May need tuning to handle the temporary high power draw of Wi-Fi during OTA.
- `web-companion/`: Will contain the GitHub Pages static app and its deployment documentation.
