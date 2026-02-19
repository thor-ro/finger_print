# Smart Door Finger (SDF) v2.0

Firmware and documentation for the Smart Door Finger (SDF) biometrics bridge device.

**Repository Layout**
* `doc/` Architecture and product documentation.
* `ref/` Reference materials and datasheets.
* `firmware/` ESP32-C6 firmware source (ESP-IDF project with `components/`).
* `tests/` Unit, integration, and hardware-in-loop tests.
* `tools/` Helper utilities used during development.
* `scripts/` Local scripts for build, flash, and dev workflows.

**Important Notes**
* BLE transport is implemented with NimBLE in central mode (`firmware/sdkconfig.defaults`) and currently targets ESP32-C6.
* Phase 1 implementation is aligned to ESP-IDF `v5.5.3` and Nuki Smart Lock 3 Pro.
* Set the real lock BLE address in `firmware/components/sdf_app/src/sdf_app.c` (`SDF_NUKI_TARGET_ADDR_TYPE` and `SDF_NUKI_TARGET_ADDR`) before first pairing. The current value is a placeholder.
* NimBLE address bytes in `ble_addr_t.val` are little-endian. Example lock address `AA:BB:CC:DD:EE:FF` must be configured as `{0xFF, 0xEE, 0xDD, 0xCC, 0xBB, 0xAA}`.
* First successful pairing stores Nuki `authorization_id` and `shared_key` in NVS via `sdf_storage`; subsequent boots reuse these credentials.
* NVS encryption is enabled. Keep `firmware/partition_table.csv` with the `nvs_keys` partition, otherwise encrypted NVS initialization will fail.
* Debug profile defaults are in `firmware/sdkconfig.debug.defaults`.
* Release profile defaults are in `firmware/sdkconfig.release.defaults`.
* Use `SDKCONFIG_DEFAULTS` when invoking `idf.py` to select a profile.
* Home Assistant/ZHA validation steps for lock/unlock/state reporting are documented in `doc/ha-validation-checklist.md`.
