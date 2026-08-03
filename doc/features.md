# Features

## 📱 Mobile App & BLE Peripheral Support
Currently, the device relies heavily on Zigbee (for remote management) or local button presses (for local management).
* [*] **Smartphone Companion App:** A BLE GATT service to allow users to configure the device, enroll fingerprints, and perform OTA updates directly from their smartphone without needing a Zigbee coordinator.
* [ ] **BLE OTA Updates:** The OTA mechanism is currently Zigbee-focused or CLI-driven. BLE-initiated OTA is marked as a "future" architectural path.

## 👥 Advanced Access Control
The current permission model is basic (Admin vs. Standard).
* [ ] **Schedule-Based Access:** The ability to restrict certain fingerprints to specific days or times (e.g., a cleaner or dog walker who can only unlock the door on Tuesdays between 10 AM and 12 PM). This would require implementing an RTC and time-sync via Zigbee.
* [ ] **Elevated User Role:** Permission level 2 ("Elevated User") is currently a placeholder. We need to define its capabilities (e.g., perhaps they can unlock the door and view logs, but cannot enroll new users).
* [*] **User Naming:** The system only tracks `User ID` (1-500). Allowing a text string to be associated with an ID (e.g., ID 42 = "Alice") would make Zigbee management and local CLI management much more user-friendly.

## 🔄 State Synchronization
* [ ] **Bidirectional Nuki Sync:** The SDF updates the Zigbee lock state when it successfully sends a BLE command to Nuki. However, if a user unlocks the Nuki lock manually from the inside, it's unclear if the SDF polls the Nuki lock to report this physical change back to the Zigbee network.

## 🔔 Hardware & Usability Enhancements
* [ ] **Audio Feedback:** The device relies entirely on the WS2812 LED ring for feedback. Adding a small piezo buzzer for auditory confirmation (success chime, error beep) would improve accessibility and user confidence.
* [ ] **Tamper Detection:** A physical tamper switch or accelerometer to detect if the device is being pried off the wall, which would immediately fire a Zigbee alarm (`0x0001` or similar).

## 🛠️ Infrastructure & Maintenance (Tech Debt)
* [ ] **Host-Based CI/CD:** The architecture notes that tests currently require physical hardware. Building a host-based test runner (using mocked hardware interfaces) is critical for scaling development and ensuring reliability.
* [ ] **Hardware Calibration:** The fingerprint LED command (`0x3C`) payload bytes are currently using defaults. We need a calibration phase to ensure the LED ring brightness and colors look premium on the final manufactured units.