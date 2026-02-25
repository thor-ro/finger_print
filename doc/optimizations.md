# Smart Door Finger (SDF) Optimization Proposals

Based on a review of the ESP-IDF C codebase, here are several optimization opportunities focused on power consumption, memory footprint, and performance.

## 1. Power Optimization: Interrupt-Driven Fingerprint Matching
**Current State ([sdf_services.c](file:///Users/thorstenropertz/workspace/smart_door/firmware/components/sdf_services/src/sdf_services.c))**:
The fingerprint service task ([sdf_services_task](file:///Users/thorstenropertz/workspace/smart_door/firmware/components/sdf_services/src/sdf_services.c#458-476)) runs a continuous polling loop, delaying for `poll_interval_ms` (default 400ms) and repeatedly asking the sensor for a match via `sdf_fingerprint_driver_match_1n`. This wakes the CPU at least every 400ms, preventing the ESP32-C6 from entering extended light sleep or deep sleep efficiently while waiting for a user.

**Optimization Proposal**:
The hardware already has a `SDF_APP_FP_WAKE_GPIO` which is used by [sdf_tasks.c](file:///Users/thorstenropertz/workspace/smart_door/firmware/components/sdf_tasks/src/sdf_tasks.c) to wake the system from light sleep.
*   **Change**: Instead of polling the UART/sensor every 400ms unconditionally, modify [sdf_services_task](file:///Users/thorstenropertz/workspace/smart_door/firmware/components/sdf_services/src/sdf_services.c#458-476) to block on a FreeRTOS Semaphore or Task Notification.
*   **Trigger**: Configure an EXTI (External Interrupt) on the `fingerprint_wake_gpio`. When the user touches the sensor, the ISR gives the semaphore to wake up [sdf_services_task](file:///Users/thorstenropertz/workspace/smart_door/firmware/components/sdf_services/src/sdf_services.c#458-476).
*   **Impact**: Massive reduction in idle power consumption, allowing the ESP32-C6 to sleep indefinitely until touched.

## 2. Memory Optimization: Stack Usage in Cryptography
**Current State ([sdf_protocol_ble.c](file:///Users/thorstenropertz/workspace/smart_door/firmware/components/sdf_protocol_ble/src/sdf_protocol_ble.c))**:
The BLE/Nuki protocol implementation uses large stack-allocated buffers for message construction and cryptography. For instance:
*   [sdf_nuki_build_encrypted_message_custom](file:///Users/thorstenropertz/workspace/smart_door/firmware/components/sdf_protocol_ble/src/sdf_protocol_ble.c#274-333): Allocates `pdata[SDF_NUKI_MAX_PDATA]` (approx 500+ bytes) and `ciphertext[SDF_NUKI_MAX_PDATA + 16]` on the stack. Total > 1KB.
*   [sdf_nuki_client_send_unencrypted](file:///Users/thorstenropertz/workspace/smart_door/firmware/components/sdf_protocol_ble/src/sdf_protocol_ble.c#485-517): Allocates `message[2 + SDF_NUKI_MAX_PDATA + 2]`. Total > 500 bytes.

**Optimization Proposal**:
Depending on the calling task's stack size (e.g., the Bluetooth event task or application task), this could risk a stack overflow if calling depths increase or if the ESP-IDF version changes task stack requirements.
*   **Change**: Add a shared transmit buffer (or unions of buffers) to the `sdf_nuki_client_t` context structure, just like `rx_buf` and `pd_buf` are reused for receiving. Alternatively, dynamically allocate these (`malloc`/`free`) if transmit operations are infrequent, to keep task stack footprints minimal.
*   **Impact**: Prevents potential stack overflows, allows reducing FreeRTOS task stack size allocations (saving internal SRAM).

## 3. Performance Optimization: Nonce Cache Lookup
**Current State ([sdf_protocol_ble.c](file:///Users/thorstenropertz/workspace/smart_door/firmware/components/sdf_protocol_ble/src/sdf_protocol_ble.c))**:
[sdf_nuki_nonce_seen()](file:///Users/thorstenropertz/workspace/smart_door/firmware/components/sdf_protocol_ble/src/sdf_protocol_ble.c#234-251) performs a linear search over `client->rx_nonce_cache_count` elements, doing a full 32-byte `memcmp` on each iteration to prevent replay attacks.

**Optimization Proposal**:
*   **Change**: If `CONFIG_SDF_SECURITY_NONCE_REPLAY_WINDOW` is large, consider comparing only the first 8 bytes (which contain the random salt or counter) first as a fast-path rejection before doing the full 32-byte `memcmp`. Since the nonce structure contains a counter and random bytes, checking a 32-bit or 64-bit aligned chunk directly is much faster than a byte-by-byte `memcmp`.

## 4. Code Size Optimization: String Formatting and Log Data
**Current State (Various files)**:
The codebase contains many switch-case functions returning strings for debug logging (e.g., [sdf_app_enrollment_result_name](file:///Users/thorstenropertz/workspace/smart_door/firmware/components/sdf_app/src/sdf_app.c#196-225), [sdf_app_error_name](file:///Users/thorstenropertz/workspace/smart_door/firmware/components/sdf_app/src/sdf_app.c#474-493), [sdf_app_zb_programming_cmd_name](file:///Users/thorstenropertz/workspace/smart_door/firmware/components/sdf_app/src/sdf_app.c#152-175)).

**Optimization Proposal**:
*   **Change**: If firmware flash size or read-only data (RODATA) segment size becomes a constraint on the ESP32-C6, these string literals and switch functions can be conditionally compiled out using `#if CONFIG_LOG_MAXIMUM_LEVEL >= ESP_LOG_INFO` or similar ESP-IDF logging macros.
*   **Impact**: Frees up Flash space for future features or OTA partition headroom.
