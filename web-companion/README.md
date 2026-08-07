# Smart Door Web Companion

This directory contains the static Web Companion app for the Smart Door Bridge.

## Features
- **Web Bluetooth (WebBLE)**: Connects directly to the ESP32-C6 over BLE.
- **Authentication**: Local SHA256 hashing for secure login and registration.
- **Device Dashboard**: Trigger HTTPS OTA updates natively via the browser.

## Deployment to GitHub Pages

Since this is a dependency-free static web application, it is automatically deployed to GitHub Pages via a GitHub Actions workflow (`.github/workflows/deploy-web-companion.yml`).

To enable this in your repository:
1. Go to your repository **Settings** on GitHub.
2. Under **Pages** > **Build and deployment** > **Source**, select **GitHub Actions**.
3. Push changes to the `web-companion/` directory, or trigger the workflow manually from the **Actions** tab.

## Browser Support
Requires a browser with Web Bluetooth support (e.g., Chrome, Edge, Chrome for Android). iOS Safari does not support Web Bluetooth natively, but specialized apps like WebBLE can be used.

## Firmware Compatibility (OTA)

Starting with this app version, the OTA (Firmware Update) flow streams the selected `.bin` file directly over the BLE GATT connection using an opcode-prefixed BEGIN(`0x01`)/CHUNK(`0x02`)/END(`0x03`) chunked transfer. This is **not** compatible with firmware built before the `replace-wifi-ota-with-ble-transfer` change (archived at `openspec/changes/archive/2026-08-07-replace-wifi-ota-with-ble-transfer/`), which expected a `{ssid, password, firmwareUrl}` JSON request and joined Wi-Fi to download the image over HTTPS.

**Firmware version floor:** devices must be running firmware built from `replace-wifi-ota-with-ble-transfer` (2026-08-07) or later before this app version is deployed against them. Deploying this app against older, pre-BLE-OTA firmware will make OTA triggering fail (the device will reject the new chunked-transfer opcodes it does not understand). Coordinate the web app deploy (GitHub Pages) and the firmware release so devices only ever pair against the app version that matches their OTA protocol.
