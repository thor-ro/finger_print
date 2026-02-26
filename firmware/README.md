# Firmware Layout (ESP-IDF Components)

This folder is structured as an ESP-IDF project with componentized modules under `components/`.

**Top-Level**
* `main/` Application entrypoint and wiring (ESP-IDF app component).
* `components/` Moduleized firmware code with explicit boundaries.

**Components**
* `components/sdf_app/` Application flows (biometric unlock, zigbee bridge, enrollment).
* `components/sdf_drivers/` Hardware drivers (fingerprint UART, LED, battery, GPIO).
* `components/sdf_protocol_ble/` BLE/Nuki protocol adaptor.
* `components/sdf_protocol_zigbee/` Zigbee Door Lock cluster adaptor.
* `components/sdf_services/` Core services (event router, power, security, config).
* `components/sdf_state_machines/` Enrollment and device state machines.
* `components/sdf_power/` FreeRTOS task definitions and scheduling glue.
* `components/sdf_platform/` ESP32-C6 HAL wrappers and system integration.
* `components/sdf_storage/` NVS storage and persistence helpers.
* `components/sdf_config/` Static configuration, constants, and defaults.
* `components/sdf_common/` Shared types and utilities used across components.

Each component should expose its public API via `include/` and keep internal code in `src/`.
