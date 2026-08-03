## Context

The Smart Door bridge currently relies on a Zigbee coordinator for configuration, user enrollment, and OTA updates. This adds complexity and hardware dependencies. This change introduces a direct smartphone interface via Web Bluetooth (WebBLE) communicating with a new BLE GATT server on the ESP32-C6. This allows secure configuration, user management, and OTA updates natively from a phone. The Web App will be hosted on GitHub Pages.

## Goals / Non-Goals

**Goals:**
- Provide a direct management interface for the device via Web Bluetooth.
- Ensure only authorized users can register and configure the device (using Admin Finger authorization).
- Support fast and reliable OTA updates by triggering a Wi-Fi download via BLE.

**Non-Goals:**
- Native iOS Safari support (Apple does not support Web Bluetooth).
- Full replacement of Zigbee functionality (Zigbee remains available for broader smart home integration).
- OTA file transfer directly over BLE (due to MTU limitations and slow speeds).

## Decisions

1. **Authentication via GATT State Machine**
   - **Decision**: Implement an unauthenticated and authenticated state in the new `ble-companion-service`.
   - **Rationale**: WebBLE connects openly; the application layer must handle login via username and password hash to unlock restricted characteristics like Enrollment, Config, and OTA.

2. **Bootstrapping via Hardware Root of Trust**
   - **Decision**: The first admin fingerprint must be enrolled using the physical hardware button on the bridge.
   - **Rationale**: Prevents an attacker from taking over an unconfigured device just by being in Bluetooth range.

3. **Web Registration Authorization**
   - **Decision**: When creating an account via the Web App, the ESP32-C6 will demand a live scan of an Admin finger before saving the new user's hashed credentials to NVS.
   - **Rationale**: Guarantees that only a physically verified admin can grant access to new web accounts.

4. **Hybrid OTA Strategy (BLE to Wi-Fi)**
   - **Decision**: The Web App sends a bounded UTF-8 JSON request containing `ssid`, `password`, and `firmwareUrl` via the authenticated OTA characteristic. The firmware accepts only `https://` URLs, connects as a Wi-Fi station using the supplied credentials, streams the response through the existing `sdf_ota_begin` / `sdf_ota_write` / verification flow, and discards the credentials once the attempt completes.
   - **Rationale**: The firmware URL supplies the source that Wi-Fi credentials alone do not identify, while reusing the firmware's existing partition, signature, and rollback protections. HTTPS avoids an unauthenticated image download.

5. **Single NimBLE Host and Service Registration**
   - **Decision**: `sdf_protocol_ble` remains the sole owner of `nimble_port_init`, NimBLE host task creation, and `ble_hs_cfg` lifecycle callbacks. The companion service registers its GATT database through a pre-start registration hook before the host task starts; on the shared host's sync callback, it starts companion advertising while the Nuki transport remains free to scan and connect as a central.
   - **Rationale**: NimBLE supports central and peripheral roles on one host, but does not support two independently initialized hosts in one process. This preserves the existing Nuki transport as the lifecycle owner and makes the companion a service provider rather than a second transport.
   - **Alternative considered**: A second NimBLE host task was rejected because it conflicts with global NimBLE initialization and host callbacks.

6. **Web App Placement and Protocol Ownership**
   - **Decision**: The GitHub Pages application will live in `web-companion/` at the repository root as dependency-free static assets (`index.html`, `assets/`, and `README.md`). It owns Web Bluetooth request encoding: username and password hashes for Auth, and the bounded OTA JSON request for OTA.
   - **Rationale**: Keeping the application in this repository makes service UUIDs and protocol changes reviewable alongside firmware while keeping deployment compatible with GitHub Pages.

## Risks / Trade-offs

- **Risk**: High power draw during Wi-Fi OTA could severely drain the battery if initiated at low charge.
  - **Mitigation**: The Web App must warn the user to ensure sufficient battery (>20%) before starting an OTA.
- **Risk**: NVS storage limits for user accounts.
  - **Mitigation**: Cap the number of web accounts (e.g., to 5) and store only secure password hashes (SHA256).
- **Risk**: BLE coexistence issues between the new GATT Server, the existing Nuki BLE Client, and the Zigbee stack.
  - **Mitigation**: Use one NimBLE host with explicit GATT registration before startup, tune role memory, and manage connection intervals to minimize radio conflicts.
- **Risk**: A malicious or malformed OTA request could exhaust memory or fetch untrusted firmware.
  - **Mitigation**: Bound every JSON field and request size, require an HTTPS URL, reject malformed input before joining Wi-Fi, and use the existing signed-image verification flow before booting the update.
- **Risk**: Wi-Fi credentials could persist beyond the OTA attempt.
  - **Mitigation**: Keep credentials only in RAM for the connection attempt and zeroize buffers after success, failure, or cancellation.

## Migration Plan

1. Add the shared NimBLE registration hook and migrate the companion service from independent host initialization to GATT registration.
2. Add the authenticated OTA request parser and Wi-Fi HTTPS downloader using the existing OTA writer and verification flow.
3. Add the `web-companion/` static application and verify registration, login, configuration, and OTA flows against the shared service UUIDs.
4. Roll back by disabling companion registration; the Nuki central and Zigbee flows remain independently functional.

## Open Questions

- The production firmware distribution URL and certificate policy must be configured before release; the characteristic accepts an HTTPS URL but must not permit certificate verification to be disabled.
