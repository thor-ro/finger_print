# Product Vision: Smart Door Finger (SDF) v2.0

The Smart Door Finger (SDF) is a dual-interface biometrics bridge that brings enterprise-grade access control to the residential smart home. It unifies local, millisecond-latency fingerprint authentication with cloud-connected remote access, all while maintaining a 100% wireless, battery-operated form factor. It acts as a secure translator: converting a user’s touch or a Zigbee command into an encrypted Bluetooth signal for the Nuki Smart Lock.

---

## 1. Strategic Goals

* **Holistic Access Management:** We move beyond simple "unlocking" to "identity management." Users can be enrolled, managed, and audited directly through their existing Smart Home interface (Home Assistant, etc.), making the SDF a fully integrated node rather than an isolated gadget.
* **Remote Concierge Mode:** The SDF serves as a remote actuator. A user can unlock their door from work for a delivery driver via their smart home app, with the SDF relaying the command to the Nuki lock over Bluetooth.
* **Battery-First "Sleepy" Architecture:** Despite receiving Zigbee commands, the device maintains a "Sleepy End Device" profile, utilizing the ESP32-C6's TWT (Target Wake Time) or Zigbee Check-in features to balance low latency for remote commands with months of battery life.

---

## 2. Functional Description

The SDF is a wall-mounted interface driven by the **Espressif ESP32-C6**. It operates in two primary modes:

1. **Biometric Mode (Local):** A user touches the **UART Fingerprint Sensor**. The device wakes, authenticates the print, and commands the Nuki lock to open via BLE.
2. **Bridge Mode (Remote):** The device listens for Zigbee 3.0 "Unlock" commands from the smart home hub. Upon receipt, it wakes its BLE stack and forwards the open command to the Nuki lock.

**User Registration:**
Registration is "Headless but Guided." Instead of buttons on the device, the user initiates "Enrollment Mode" via their Smart Home App. The SDF uses its LED ring (on the sensor) to guide the user through the required 3-touch calibration process defined in the sensor datasheet.

---

## 3. Technical Architecture & Component Utilization

### A. Microcontroller: ESP32-C6 (SoC)

* **Role:** Zigbee End Device & BLE Central.
* **Zigbee Profile:** Implements the **Zigbee Home Automation (ZHA)** profile, specifically the **Door Lock Cluster (0x0101)**.
* **Dual-Stack Operation:**
* *Ingress:* Receives `Lock`, `Unlock`, and `Add User` commands via Zigbee (802.15.4).
* *Egress:* Sends encrypted `0x000D` (Lock Action) commands to Nuki via Bluetooth LE.

### B. Biometrics: UART Fingerprint Sensor (C)

* **Registration Logic:** The ESP32-C6 manages the enrollment state machine. When triggered by Zigbee, the ESP32 sends the `Add User (0x01)` command to the sensor, waits for the finger press (monitoring the `WAKE` pin), sends `Add User 2 (0x02)`, and finally `Add User 3 (0x03)` to merge the characteristics into a template.
* **Storage:** Fingerprint templates are stored securely in the sensor's local flash memory (up to 500 users).

### C. Network Integration: Zigbee 3.0

* **Cluster 0x0101 (Door Lock):**
* **Command:** `Unlock Door` -> Triggers ESP32 to send Nuki BLE unlock.
* **Command:** `Lock Door` -> Triggers ESP32 to send Nuki BLE lock.
* **Command:** `Programming Event` -> Triggers "Enrollment Mode" on the sensor.


* **Attributes:** Reports `Lock State`, `Battery Percentage`, and `Alarm Mask` (for failed biometric attempts).

---

## 4. Vision Paths (User Journeys)

### Path A: The "Come Home" (Biometric Unlock)

1. **Trigger:** User touches the sensor.
2. **Wake:** Sensor asserts `WAKE` pin; ESP32-C6 boots from deep sleep.
3. **Verify:** ESP32 queries sensor; Sensor returns `User ID: 5`.
4. **Action:** ESP32 connects to Nuki via BLE and sends `Unlock` command.
5. **Log:** ESP32 sends Zigbee report: `User 5 Unlocked via Local Biometric`.

### Path B: The "Remote Guest" (Zigbee Bridge)

1. **Trigger:** Homeowner taps "Unlock" in their Smart Home App (e.g., Home Assistant).
2. **Transmission:** The Zigbee Coordinator queues the command.
3. **Reception:** The SDF (waking periodically on its check-in interval) receives the `Unlock` payload.
4. **Bridge:** ESP32 activates BLE, connects to Nuki, and executes the unlock.
5. **Feedback:** SDF reports `Unlocked` status back to the Smart Home App.

### Path C: The "New Family Member" (Registration)

1. **Initiate:** Homeowner selects "Add User" in the Smart Home App and assigns ID #06.
2. **Mode Switch:** SDF enters `Enrollment Mode`. The Sensor LED flashes blue (slow breath).
3. **Guide:**
* User places finger. Sensor LED flashes green once.
* User lifts and places finger again. Sensor LED flashes green twice.
* User places finger a third time. Sensor LED stays solid green for 2 seconds.


4. **Finalize:** ESP32 receives "Success" from the sensor and reports `User 06 Added` to the Smart Home system.

---

## 5. Implementation Roadmap Updates

* **Phase 1 (Core):** Basic Fingerprint-to-Nuki unlock (as before).
* **Phase 2 (Zigbee Stack):** Implementing the ESP32-C6 Zigbee End Device logic. Mapping the "Door Lock Cluster" commands to internal functions.
* **Phase 3 (The Bridge Logic):** Creating the state machine that allows the ESP32 to handle the complex timing of `Zigbee Command -> Wake BLE -> Connect Nuki -> Send Command -> Report to Zigbee`.
* **Phase 4 (Enrollment UX):** Developing the LED feedback patterns on the sensor (using the `Control LED` command 0x3C in the datasheet) to guide the user during the multi-step registration process without a screen.