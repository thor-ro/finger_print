# System Architecture Specification: Smart Door Finger (SDF) v2.0

## 1. Purpose
Define the system architecture for Smart Door Finger (SDF) v2.0 based on the product vision in `doc/vision.md`. This specification describes components, interfaces, data flows, and key non‑functional requirements to enable implementation and verification.

## 2. Scope
This specification covers the SDF device, its firmware, and its integrations with:
* Zigbee 3.0 smart home coordinators (ZHA profile, Door Lock Cluster 0x0101).
* Nuki Smart Lock over Bluetooth LE.
* The UART fingerprint sensor and its LED ring.

Out of scope:
* Backend cloud services (none required in the current vision).
* Mobile app UI design (assumed to be provided by existing smart home apps).

## 3. Architecture Drivers (From Vision)
* Dual‑interface access: local biometric unlock and remote Zigbee unlock.
* Battery‑first sleepy end device profile with low latency for remote commands.
* Headless enrollment initiated via smart home app, guided by LED feedback.
* Secure translation of local/remote inputs into encrypted BLE commands to Nuki.

## 4. System Context
**Actors**
* User/Homeowner
* Guest/Delivery person
* Smart Home Coordinator (e.g., Home Assistant Zigbee coordinator)
* Nuki Smart Lock

**High‑Level Context**
* SDF is a Zigbee End Device and BLE Central.
* SDF is physically mounted near the door and communicates with the fingerprint sensor via UART.
* SDF bridges Zigbee commands to BLE lock actions.

## 5. Components

### 5.1 Hardware
* **ESP32‑C6 SoC**
  * Zigbee 802.15.4 End Device (ZHA profile).
  * BLE Central for Nuki lock.
  * Deep sleep and timed wakeups (TWT/Check‑in).
* **UART Fingerprint Sensor**
  * Template storage in sensor flash (up to 500 users).
  * WAKE pin for touch detection.
  * LED ring for enrollment feedback (Control LED 0x3C).
* **Power Subsystem**
  * Battery‑powered, optimized for long sleep intervals.
* **Nuki Smart Lock**
  * Receives encrypted BLE 0x000D Lock Action commands.

### 5.2 Firmware Modules
* **Zigbee Stack**
  * ZHA Door Lock Cluster (0x0101) commands and attributes.
* **BLE Central Manager**
  * Nuki pairing, secure connection, and command dispatch.
* **Fingerprint Driver**
  * UART protocol for matching and enrollment commands.
  * LED ring control.
* **Enrollment State Machine**
  * 3‑touch guided enrollment flow.
* **Power Manager**
  * Sleep scheduling, wake sources, and radio power gating.
* **Event Router**
  * Routes inputs (fingerprint/Zigbee) to lock actions and reports.
* **Security Manager**
  * Key storage, encrypted communications, rate limiting.
* **Telemetry/Logging**
  * Reports Zigbee attributes and audit events.

## 6. Interfaces

### 6.1 Zigbee (ZHA, Door Lock Cluster 0x0101)
**Commands (Ingress)**
* `Unlock Door` -> trigger BLE unlock
* `Lock Door` -> trigger BLE lock
* `Programming Event` -> trigger Enrollment Mode

**Attributes (Egress)**
* `Lock State`
* `Battery Percentage`
* `Alarm Mask` (e.g., failed biometric attempts)

### 6.2 BLE (Nuki Smart Lock)
* **Role:** SDF is BLE Central; Nuki is Peripheral.
* **Command:** Encrypted `0x000D` Lock Action (lock/unlock).
* **Connection Constraints:** Short session, disconnect immediately after command confirmation.

### 6.3 Fingerprint Sensor (UART + GPIO)
* **GPIO:** `WAKE` pin indicates touch.
* **Enrollment Commands:** `Add User (0x01)`, `Add User 2 (0x02)`, `Add User 3 (0x03)`.
* **LED Guidance:** `Control LED (0x3C)` for breathing/solid colors.
* **Matching:** Sensor returns `User ID` for successful match.

## 7. Data Model
* **User ID Mapping**
  * Zigbee user ID -> Sensor template ID (1:1).
  * Max users: 500 (sensor limit).
* **Lock State**
  * `Locked`, `Unlocked`, `Unknown` (if Nuki not reachable).
* **Battery**
  * Percentage reported over Zigbee attribute.
* **Audit Events**
  * Local biometric unlocks.
  * Zigbee command unlocks/locks.
  * Enrollment events.
  * Failed biometric attempts.

## 8. Operational Flows

### 8.1 Biometric Unlock (Local)
1. Sensor asserts `WAKE`.
2. ESP32‑C6 wakes from deep sleep.
3. ESP32 queries sensor; sensor returns `User ID`.
4. BLE central connects to Nuki and sends `Unlock`.
5. Zigbee reports `User X Unlocked via Local Biometric`.

### 8.2 Zigbee Bridge Unlock (Remote)
1. Smart home app sends `Unlock Door` to coordinator.
2. Zigbee coordinator queues command.
3. SDF wakes on check‑in interval, receives command.
4. BLE central connects to Nuki and sends `Unlock`.
5. Zigbee reports updated `Lock State`.

### 8.3 User Enrollment (Headless Guided)
1. Homeowner initiates `Add User` in smart home app.
2. SDF enters Enrollment Mode; LED ring breathes blue.
3. User touches three times; LED indicates progress (green flashes).
4. ESP32 receives success from sensor.
5. Zigbee reports `User Added`.

## 9. Power Management
* **Sleepy End Device**
  * Primary state is deep sleep.
  * Wake sources: fingerprint `WAKE` pin, Zigbee check‑in/timers.
* **Latency Targets**
  * Local biometric: millisecond‑level response after touch.
  * Remote command: bounded by check‑in interval.
* **Radio Gating**
  * Zigbee and BLE enabled only for command processing.
  * BLE session minimized to reduce power draw.

## 10. Security
* **Zigbee Security**
  * Use Zigbee 3.0 link keys and standard encryption.
* **BLE Security**
  * Encrypted command channel to Nuki.
* **Fingerprint Privacy**
  * Templates stored only on sensor flash.
  * No biometric data transmitted over Zigbee/BLE.
* **Abuse Protection**
  * Rate limiting for failed biometric attempts.
  * Alarm mask updates on repeated failures.

## 11. Reliability and Error Handling
* **BLE Errors**
  * Retry on transient failure (bounded).
  * Report `Unknown` lock state if Nuki unreachable.
* **Zigbee Command Failures**
  * Report failure via attributes; do not block future commands.
* **Enrollment Failures**
  * Reset state machine and notify via Zigbee.

## 12. Open Questions / TBD
* Zigbee check‑in interval and target latency balance.
* Exact retry limits and backoff strategy for BLE.
* Final battery size and expected months of operation.
* OTA firmware update mechanism (if required).

## 13. Verification Checklist
* Biometric unlock triggers BLE unlock within target latency.
* Zigbee `Unlock Door` triggers BLE unlock and reports `Lock State`.
* Enrollment flow guides user with LED ring and stores template.
* Sleep behavior meets battery‑life targets.
* Security requirements met (no biometric leakage, encrypted channels).
