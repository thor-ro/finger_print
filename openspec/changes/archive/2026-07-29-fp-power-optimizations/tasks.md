## 1. UART Baud Rate Update

- [x] 1.1 Update `CONFIG_SDF_FP_BAUD_RATE` to 115200 in `firmware/sdkconfig.defaults`.
- [x] 1.2 Update the baud rate in the Kconfig file (`firmware/components/sdf_config/Kconfig` or similar) to change the default value.

## 2. Match Task Refactoring

- [x] 2.1 Identify the WAKE GPIO pin mapping in `firmware/components/sdf_drivers/include/sdf_drivers.h` (or similar) and ensure the ISR is configured to notify `sdf_match_task`.
- [x] 2.2 Modify `sdf_match_task` in `firmware/components/sdf_services/src/sdf_services.c` (or `sdf_match_task.c`) to block on a queue/event group instead of polling at `CONFIG_SDF_MATCH_POLL_INTERVAL_MS`.
- [x] 2.3 Connect the GPIO WAKE ISR to emit `SDF_EVENT_ROUTER_BIOMETRIC_MATCH_REQUEST` or unblock the task directly.

## 3. Testing and Validation

- [ ] 3.1 Build and flash the firmware to test device to confirm the 115200 baud UART communication is stable.
- [ ] 3.2 Verify the match task is awoken promptly when a finger is placed on the sensor and no continuous polling occurs.
