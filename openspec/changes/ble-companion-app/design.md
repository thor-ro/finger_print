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
   - **Decision**: The Web App passes Wi-Fi credentials via BLE to the ESP32-C6, which then downloads the firmware directly via Wi-Fi.
   - **Rationale**: Avoids the slow transfer speeds and packet drop issues of pushing a 1MB+ firmware image over BLE. The Web App will warn the user regarding temporary battery drain.

## Risks / Trade-offs

- **Risk**: High power draw during Wi-Fi OTA could severely drain the battery if initiated at low charge.
  - **Mitigation**: The Web App must warn the user to ensure sufficient battery (>20%) before starting an OTA.
- **Risk**: NVS storage limits for user accounts.
  - **Mitigation**: Cap the number of web accounts (e.g., to 5) and store only secure password hashes (SHA256).
- **Risk**: BLE coexistence issues between the new GATT Server, the existing Nuki BLE Client, and the Zigbee stack.
  - **Mitigation**: Tune the NimBLE configuration to allocate sufficient memory for multiple roles and manage connection intervals to minimize radio conflicts.
