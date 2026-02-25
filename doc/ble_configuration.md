# BLE Smartphone Configuration Concept

The user has inquired about configuring the Smart Door Lock system via a companion Android application over Bluetooth Low Energy (BLE).

## 1. Current BLE Architecture Assessment

Currently, the `sdf_protocol_ble` component acts **exclusively as a Central (Client)** device using the Apache NimBLE stack. Its singular purpose is to scan for, connect to, and control an existing Nuki Smart Lock Peripheral.

### Limitations of the Current Stack:
- **No Advertisements:** The ESP32-C6 does not advertise its availability to external devices like smartphones.
- **No Local GATT Server:** The firmware does not host its own attributes or characteristics for external devices to read or write. It only performs GATT discoveries on the target Nuki lock.
- **Radio Gating:** The power management task currently utilizes Zigbee sleep windows to aggressively gate the BLE radio (`CONFIG_SDF_POWER_ENABLE_BLE_RADIO_GATING`), meaning a smartphone would not be able to connect reliably while the lock is resting between Zigbee check-ins.

## 2. Feasibility and Implementation Strategy

It **is entirely possible** to implement connection capabilities for an Android companion app, provided the NimBLE stack is reconfigured.

### Step 1: Enable Dual-Role BLE (Central + Peripheral)
The Apache NimBLE stack supports concurrent Central and Peripheral roles. The firmware must be updated to:
1. Act as a **Peripheral**: Start advertising a custom `SDF_CONFIGURATION_SERVICE_UUID` so smartphones can discover it.
2. Spin up a local **GATT Server** storing configuration characteristics (e.g., Zigbee Network Keys, Fingerprint Sensitivity, OTA triggers).

### Step 2: Custom Configuration Service Design
A dedicated GATT Service should be created to manage configurations securely.
**Example Characteristics:**
- `0x1001`: **System Status** (Read-Only) - Hardware health, Battery %, Current Lock State.
- `0x1002`: **Network Configuration** (Read/Write) - Zigbee coordinates, Wi-Fi fallback credentials.
- `0x1003`: **Device Parameters** (Read/Write) - Polling intervals, Fingerprint match thresholds.
- `0x1004`: **Biometric Management** (Write-Only) - Trigger local biometric enrollment without utilizing the Zigbee network.

### Step 3: Security & Pairing
To prevent unauthorized configurations from malicious actors bridging the lock via BLE:
- **Just Works vs OOB Pairing:** The companion app should implement bonded pairing (Numeric Comparison if a screen was present, or a pre-shared Out-of-Band key programmed at the factory or printed on a QR code).
- **Application-Layer Encryption:** Alternatively, adopt an end-to-end encrypted packet structure payload (similar to the Nuki protocol) where the characteristic payloads are signed via ChaCha20-Poly1305.

### Step 4: Power Management Adjustments
If the lock acts as a Peripheral, it must continually broadcast advertisements. Advertising increases baseline power consumption significantly.
- **Solution:** Implement a "Configuration Mode Button". The BLE Peripheral role and advertisements should normally remain disabled. If the user presses a physical button on the lock (or hold their finger on the biometric sensor for 10 seconds), the lock enters an active BLE Advertising mode for 5 minutes, pausing Zigbee sleep gating.

## Conclusion
While the current firmware firmware is not instantly structured to accept connections from Android devices, upgrading the NimBLE component to support Peripheral roles alongside a custom Authentication & Configuration GATT service is technically feasible and highly recommended for initial consumer setup.
