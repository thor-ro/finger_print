# Smart Door Web Companion

This directory contains the static Web Companion app for the Smart Door Bridge.

## Features
- **Web Bluetooth (WebBLE)**: Connects directly to the ESP32-C6 over BLE.
- **First-Time Setup Wizard**: Mandatory, guided flow for claiming an unclaimed device (Admin enrolment → account registration → Nuki pairing → explicit finish).
- **Authentication**: Local SHA256 hashing for secure login and registration (challenge-response LOGIN).
- **Admin-Bound Accounts**: A companion account is an attribute of a fingerprint user, not a standalone record. The name you submit at registration is your name **on the device** (and must be unique), and the account belongs to the admin whose fingerprint scan confirms it. Session authority is derived live from that admin's current permission: demoting or deleting the bound admin immediately removes the account's access.
- **Re-Registration = Password Reset**: Registering again with an admin's scan replaces that admin's existing password in place — this is the supported way to reset a forgotten password. The app warns before submitting for exactly this reason.
- **Device Dashboard**: Config, enrollment, Nuki re-pair, Zigbee join and OTA updates natively via the browser.

## First-Time Setup Wizard

A brand-new or factory-reset device enters a **setup phase**: it advertises openly so any companion can connect. When this app connects to a device whose setup state characteristic reports "not complete", it presents the setup wizard instead of the login form:

1. **Enrol the Admin Fingerprint** — three scans; creates User ID 1 with admin permission.
2. **Register Your Account** — offered only after the Admin exists (registration is confirmed with an Admin finger scan).
3. **Pair Your Nuki Lock** — put the lock into pairing mode first, then start pairing from the wizard.
4. **Finish Setup** — an explicit completion request. On success the device locks itself to this browser's companion and switches to filtered advertising.

The wizard reads the device's setup state before login and resumes at the reported step after reconnects within one setup phase.

> [!IMPORTANT]
> The setup phase is **time-bounded** (about 15 minutes per arm). If the window lapses mid-wizard, all progress is erased on the device and you must press its physical button to re-arm and start over.

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
