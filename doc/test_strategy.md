# Smart Door Firmware (SDF) Test Strategy

This document outlines the approach for developing and maintaining automated tests for the ESP32-C6 Smart Door project.

## 1. Testing Framework
The project uses **Unity**, the standard C unit testing framework integrated tightly with ESP-IDF. 

## 2. Test Architecture
Tests are implemented alongside the modular components they verify. 
- **Location:** `firmware/components/<component_name>/test/`
- Every test file is named `test_<module>.c`.
- **Test Runner App:** A separate ESP-IDF project will be created at `firmware/test_runner/` that links against all internal components and executes interactive or automated test runs on hardware (or QEMU/Host).

## 3. Test Categories and Scope

### 3.1. Logic and State (High Priority)
Components that execute complex logic but do not strictly require access to peripheral registers. These are the easiest to test and provide high ROI.
- **Component:** `sdf_state_machines`
- **Scope:** Enrollment sub-state transitions, authentication verification, and lock/unlock condition checks.

### 3.2. Data Serialization and Storage (Medium Priority)
Components that handle reading/writing data formats to Non-Volatile Storage.
- **Component:** `sdf_storage`
- **Scope:** Credential structural limits, default recovery behavior, and proper NVS handle initialization. Requires NVS host mocking or execution on the ESP32-C6 flash.

### 3.3. Protocol Drivers (Medium Priority)
Components dealing with framing, parsing, and verifying external payloads.
- **Component:** `sdf_drivers` (UART fingerprint protocol)
- **Scope:** Checksum calculations, response definitions mapping, and packet assembly. Hardware UART operations should be decoupled or mocked.

### 3.4. Integration/System Tests (Low Priority Unit, High Priority QA)
Testing interactions between BLE, Zigbee, FreeRTOS tasks, and real GPIO.
- Generally these require a Hardware-in-the-Loop (HIL) setup using `pytest` to drive the UART and verify Zigbee/BLE packet captures using a coordinator module or sniffer.

## 4. Mocking Strategy
For strictly hardware-bound logic, decoupling abstractions (via function pointers or `#define` injection) should be employed. If we expand to Linux Host testing (`idf.py --preview set-target linux`), FreeRTOS primitives and ESP hardware drivers must be bypassed using the ESP-IDF Linux Mocks. Initially, the focus is on testing natively on the ESP32-C6.

## 5. Running Tests
Once the `firmware/test_runner/` app is built and flashed, it boots and invokes `unity_run_menu()`. The developer can monitor the console to see all available tests and run them sequentially or interactively.
