# FreeRTOS Tasks

This document outlines the FreeRTOS tasks actively created and managed by the Smart Door Finger (SDF) firmware.

## 1. `sdf_zigbee` (Zigbee Stack Task)
- **Entry Function:** `sdf_zigbee_task`
- **Location:** `firmware/components/sdf_protocol_zigbee/src/sdf_protocol_zigbee.c`
- **Stack Size:** `6144` bytes (`SDF_ZIGBEE_TASK_STACK`)
- **Priority:** `5` (`SDF_ZIGBEE_TASK_PRIORITY`)
- **Description:** Runs the core `esp_zb_app_signal_handler`. It sits in a continuous loop yielding to the Zigbee network stack, processing incoming ZCL cluster commands (like Lock/Unlock/Toggle), handling OTA upgrade callbacks, and responding to attribute read requests.

## 2. `sdf_fp` (Fingerprint Services Task)
- **Entry Function:** `sdf_services_task`
- **Location:** `firmware/components/sdf_services/src/sdf_services.c`
- **Stack Size:** `4096` bytes (`SDF_SERVICES_TASK_STACK`)
- **Priority:** `5` (`SDF_SERVICES_TASK_PRIORITY`)
- **Description:** Manages the fingerprint sensor driver and biometrics. It handles hardware interrupts, polls for touch detection, executes the fingerprint matching algorithm, processes enrollment states, and triggers security lockout timers upon repeated failed attempts.

## 3. `sdf_power` (Power & Maintenance Task)
- **Entry Function:** `sdf_tasks_task`
- **Location:** `firmware/components/sdf_tasks/src/sdf_tasks.c`
- **Stack Size:** `4096` bytes (`SDF_POWER_TASK_STACK`)
- **Priority:** `4` (`SDF_POWER_TASK_PRIORITY`)
- **Description:** Oversees periodic system maintenance and power management. It governs deep sleep timing logic, sends out periodic battery percentage reports locally and over the Zigbee network, and acts as a background watchdog for system inactivity timeouts.

## 4. `nimble_host` (BLE Communication Task)
- **Entry Function:** `sdf_nuki_ble_host_task` (via ESP-IDF NimBLE wrapper)
- **Location:** `firmware/components/sdf_protocol_ble/src/sdf_nuki_ble_transport.c`
- **Stack Size:** Managed by ESP-IDF menuconfig (`CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE`)
- **Priority:** Managed by ESP-IDF menuconfig (`CONFIG_BT_NIMBLE_HOST_TASK_PRIORITY`)
- **Description:** This task is not explicitly instantiated with `xTaskCreate` in application code, but rather spawned using `nimble_port_freertos_init`. It runs the Bluetooth Low Energy host stack. It handles establishing and maintaining secure encrypted connections, discovering custom Nuki GATT characteristics, and routing pairing payloads with the smart lock keyturner over BLE.
