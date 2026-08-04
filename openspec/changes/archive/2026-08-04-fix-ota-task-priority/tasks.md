## 1. Fix Priority

- [x] 1.1 In `sdf_ble_companion_ota.c`, define `#define SDF_BLE_OTA_TASK_PRIORITY (tskIDLE_PRIORITY + 8)` near the other constants
- [x] 1.2 Replace `tskIDLE_PRIORITY + 2` with `SDF_BLE_OTA_TASK_PRIORITY` in the `xTaskCreate()` call

## 2. Build & Verify

- [x] 2.1 Build firmware, confirm no compile errors
- [x] 2.2 Verify task priorities are documented in `doc/rtos_tasks.md` — update if the OTA task is listed there
