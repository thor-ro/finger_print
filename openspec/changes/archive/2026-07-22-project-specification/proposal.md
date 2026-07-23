## Why

This project creates a complete OpenSpec specification for the Smart Door Finger (SDF) v2.0 firmware — an ESP32-C6 biometrics bridge that translates Zigbee Door Lock commands and fingerprint matches into encrypted BLE lock actions for the Nuki Smart Lock 3 Pro. The specification documents the architecture, requirements, and implementation details to enable future development, testing, and compliance verification.

## What Changes

- Create a complete specification suite for the SDF v2.0 firmware project
- Document all 11 ESP-IDF components with their public APIs, responsibilities, and interactions
- Specify the three primary runtime flows: Biometric Unlock, Zigbee Bridge, and Enrollment
- Define security requirements: nonce replay protection, biometric rate limiting, encrypted NVS
- Specify power management: deep sleep, Zigbee check-in, BLE radio gating
- Define build, test, and deployment procedures for ESP-IDF v5.5.3
- Establish documentation sync rules for architecture changes

## Capabilities

### New Capabilities

- `sdf-app`: Application orchestration for biometric unlock flow, Zigbee bridge flow, Nuki pairing flow, enrollment trigger, lock action queuing, BLE transport management, and audit event emission
- `sdf-services`: Core services including event router, fingerprint match polling cycle, enrollment execution, admin authorization cycle, security rate limiting, LED feedback dispatch, button handler
- `sdf-protocol-ble`: BLE/Nuki protocol adapter with message framing, encryption/decryption (Curve25519 ECDH, HMAC-SHA256, libsodium secretbox), Nuki client state machine, pairing handshake
- `sdf-protocol-zigbee`: Zigbee Door Lock Cluster (0x0101) adapter for command reception (Lock/Unlock/Latch/Programming), attribute reporting (Lock State, Battery, Alarm Mask, User List)
- `sdf-drivers`: Hardware abstraction layer for fingerprint UART driver (19200 baud, proprietary protocol), WS2812 LED ring driver, battery ADC driver, GPIO power gating
- `sdf-state-machines`: Pure-logic enrollment state machine (IDLE → STEP_1 → STEP_2 → STEP_3 → SUCCESS/ERROR), device state machine
- `sdf-storage`: NVS persistence for Nuki credentials (authorization_id + shared_key), BLE target address, security policy, with encrypted NVS verification at boot
- `sdf-power`: FreeRTOS power manager with sleep/wake scheduling, Zigbee check-in coordination, BLE radio gating, battery reporting, deep sleep default
- `sdf-platform`: ESP32-C6 HAL wrappers for GPIO, UART, ADC, RMT, FreeRTOS primitives
- `sdf-common`: Shared types (lock action enums, keyturner state, event/audit structs, error codes), utilities, mock interfaces
- `sdf-cli`: Debug CLI for interactive testing and diagnostics

- `security-policy`: Security defaults and requirements — nonce replay window (8 entries), biometric fail threshold (5 attempts/60s → 120s lockout), encrypted NVS required at boot, Zigbee alarm bits for lockout (0x0004) and security protocol (0x0008)

- `power-management`: Power management state machine (SLEEP → WAKE_FINGER/WAKE_ZIGBEE → ACTIVE → BLE_ACTION → REPORT → SLEEP), configurable intervals (check-in 15s, idle 5s, guard 1.5s), BLE radio on-demand gating

- `build-system`: ESP-IDF v5.5.3 build configuration with debug/release profiles, partition table (NVS, OTA_0/1, nvs_keys, zb_storage, zb_fct), test runner project for Unity tests on hardware

- `enrollment-flows`: Local enrollment (button press → admin auth → 3-step fingerprint enrollment), remote enrollment (Zigbee Set PIN/RFID Code → enrollment trigger), first-time setup (unclaimed device → admin enrollment → Nuki pairing → Zigbee join)

### Modified Capabilities

None — this is a new specification for an existing project.

## Impact

- **Code**: `firmware/components/*` (11 ESP-IDF components), `firmware/test_runner/` (test project), `firmware/sdkconfig.*` (build configs), `firmware/partition_table.csv`
- **Documentation**: `doc/sdf_sas.md` (software architecture), `doc/user_manual.md`, `AGENTS.md`, `version.md`
- **Build**: ESP-IDF v5.5.3 toolchain, NimBLE stack, ESP-Zigbee stack
- **Hardware**: ESP32-C6, UART fingerprint sensor, WS2812 LED, Nuki Smart Lock 3 Pro, Zigbee coordinator
- **Testing**: Unity test framework on hardware via `test_runner` project