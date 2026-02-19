# Software Architecture Specification: Smart Door Finger (SDF) v2.0

## 1. Purpose
Define the software architecture for the Smart Door Finger (SDF) v2.0 firmware that runs on the ESP32‑C6. This document describes modules, runtime design, interfaces, state machines, and quality attributes needed to implement the product vision in `doc/vision.md`.

## 2. Scope
This specification covers:
* ESP32‑C6 firmware architecture.
* Zigbee 3.0 ZHA Door Lock cluster integration.
* BLE Central integration with Nuki Smart Lock.
* UART fingerprint sensor integration and enrollment flow.

Out of scope:
* Mobile or web UI code.
* Cloud backend services.

## 3. Architecture Drivers
* Dual‑mode access: biometric and Zigbee remote commands.
* Battery‑first design with sleepy end device behavior.
* Headless enrollment with LED guidance.
* Secure translation of Zigbee and biometric events to BLE lock actions.
* Reliable event reporting and auditability.

## 4. Software Context
**External Systems**
* Zigbee Coordinator (ZHA)
* Nuki Smart Lock (BLE Peripheral)
* UART Fingerprint Sensor

**Software Boundary**
* All firmware resides on ESP32‑C6 and communicates with external systems via Zigbee, BLE, UART, and GPIO.

## 5. Architectural Overview
**Layered Model**
* **Hardware Abstraction**
  * UART, GPIO, BLE, Zigbee, NVS, timers, sleep APIs.
* **Drivers**
  * Fingerprint sensor driver, LED control, battery/ADC driver.
* **Core Services**
  * Event router, power manager, security manager, configuration store.
* **Protocol Adaptors**
  * Zigbee Door Lock cluster adaptor, BLE Nuki adaptor.
* **Application Logic**
  * Biometric unlock, Zigbee bridge, enrollment state machine.

## 6. Runtime Design
**Execution Model**
* FreeRTOS tasks with event queues and short‑lived BLE sessions.
* Interrupt‑driven wake from sensor `WAKE` pin and periodic Zigbee check‑ins.

**Primary Tasks**
* `task_event_router` handles event dispatch and state transitions.
* `task_zigbee` manages ZHA commands, attribute reporting, and check‑ins.
* `task_ble` manages Nuki connections and lock actions.
* `task_fingerprint` manages UART sensor commands and match/enroll flows.
* `task_power` manages sleep entry, wake sources, and radio gating.

**Concurrency Rules**
* Only one lock action (BLE session) at a time.
* Enrollment mode blocks biometric unlock attempts.
* Zigbee commands are queued while BLE is active.

## 7. Module Decomposition
**Event Router**
* Central event bus with typed events and priority levels.
* Normalizes inputs from Zigbee, sensor, and timers.

**Zigbee Door Lock Adapter**
* Maps ZHA Door Lock commands to internal events.
* Reports attributes and audit events.

**BLE Nuki Adapter**
* Handles pairing, encryption, and `0x000D` Lock Action.
* Enforces connection timeouts and retries.

**Fingerprint Driver**
* UART protocol handling.
* Match queries and enrollment commands (`0x01`, `0x02`, `0x03`).
* LED control (`0x3C`).

**Enrollment State Machine**
* Guides 3‑touch enrollment with LED feedback.
* Persists user mapping and reports status.

**Power Manager**
* Coordinates sleep entry and wake sources.
* Manages Zigbee check‑in interval and BLE gating.

**Security Manager**
* Key storage and access control.
* Rate limiting for failed biometric attempts.

**Configuration Store**
* NVS‑based storage for user map, pairing keys, and device settings.

## 8. Interfaces and Contracts

### 8.1 Zigbee Door Lock Cluster (0x0101)
**Ingress Commands**
* `Unlock Door` -> `EVT_ZB_UNLOCK`
* `Lock Door` -> `EVT_ZB_LOCK`
* `Programming Event` -> `EVT_ZB_ENROLL_START`

**Egress Attributes**
* `Lock State`
* `Battery Percentage`
* `Alarm Mask`

### 8.2 BLE to Nuki
* `BLE_CONNECT(timeout_ms)`
* `BLE_LOCK_ACTION(action, user_id)`
* `BLE_DISCONNECT()`

### 8.3 Fingerprint Sensor
* `FP_MATCH()` -> returns `user_id` or `NO_MATCH`
* `FP_ENROLL_STEP(n)` where `n` in {1,2,3}
* `FP_LED(mode, color, duration_ms)`

## 9. Data Model
* `UserId` integer in range 1..500.
* `LockState` enum: `LOCKED`, `UNLOCKED`, `UNKNOWN`.
* `AuditEvent` struct with timestamp, user_id, source, result.
* `DeviceConfig` struct with Zigbee check‑in interval, retry limits, and provisioning metadata.

## 10. State Machines

### 10.1 Device State
* `SLEEP` -> `WAKE_BIOMETRIC` on `WAKE` pin.
* `SLEEP` -> `WAKE_ZIGBEE` on check‑in timer.
* `WAKE_BIOMETRIC` -> `BLE_ACTION` after match.
* `WAKE_ZIGBEE` -> `BLE_ACTION` after command.
* `BLE_ACTION` -> `REPORT` -> `SLEEP`.

### 10.2 Enrollment State
* `IDLE` -> `ENROLL_STEP1` -> `ENROLL_STEP2` -> `ENROLL_STEP3` -> `SUCCESS`.
* Failure at any step -> `ERROR` -> `IDLE`.

## 11. Power Management Integration
* Default to deep sleep.
* Zigbee check‑in period is configurable and coordinated with power manager.
* BLE and Zigbee radios only enabled during active processing.

## 12. Security Considerations
* Zigbee 3.0 encryption using standard link keys.
* BLE communication encrypted, Nuki pairing keys stored in NVS.
* Fingerprint templates never leave sensor.
* Rate limiting and alarm mask updates for repeated failures.

## 13. Error Handling and Resilience
* BLE failures retry up to configurable limit with backoff.
* Zigbee command failures reported via attributes.
* Enrollment failures reset state machine and notify via Zigbee.
* Watchdog resets recover from stalled tasks.

## 14. Observability
* Event logs with ring buffer in RAM.
* Summary audit events reported to Zigbee.
* Diagnostic counters for BLE errors, Zigbee timeouts, and enrollment failures.

## 15. Configuration and Provisioning
* Zigbee commissioning during initial setup.
* BLE pairing flow with Nuki stored in NVS.
* Configurable parameters for check‑in interval, retry limits, and LED patterns.

## 16. Build and Update
* Target: ESP‑IDF toolchain with FreeRTOS.
* Semantic versioning for firmware releases.
* OTA update mechanism TBD.

## 17. Test Strategy
* Unit tests for driver parsers and state machines.
* Integration tests with Zigbee coordinator and Nuki lock.
* Hardware‑in‑loop tests for biometric enrollment and power sleep/wake.

## 18. Open Questions / TBD
* Exact Zigbee check‑in interval and acceptable latency.
* BLE retry/backoff values.
* OTA update approach.
* Final log retention size and export policy.
