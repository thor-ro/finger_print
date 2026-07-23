## 1. sdf_storage — Add erase_all and ble_target_clear

- [x] 1.1 Add `sdf_storage_erase_all()` declaration to `sdf_storage.h`
- [x] 1.2 Add `sdf_storage_ble_target_clear()` declaration to `sdf_storage.h`
- [x] 1.3 Implement `sdf_storage_erase_all()` in `sdf_storage.c` (call `nvs_flash_erase()` + `nvs_flash_init()`)
- [x] 1.4 Implement `sdf_storage_ble_target_clear()` in `sdf_storage.c` (erase "ble_target" key)
- [x] 1.5 Add unit tests in `test_sdf_storage.c` for both new functions

## 2. sdf_protocol_zigbee — Add factory_reset

- [x] 2.1 Add `sdf_protocol_zigbee_factory_reset()` declaration to `sdf_protocol_zigbee.h`
- [x] 2.2 Implement `sdf_protocol_zigbee_factory_reset()` in `sdf_protocol_zigbee.c`:
  - Call `esp_zb_bdb_factory_reset()`
  - If network joined, call `esp_zb_bdb_leave_network()`
  - Reinitialize with `esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_INITIALIZATION)`
- [x] 2.3 Add unit test in `test_sdf_protocol_zigbee.c` (mock ESP-ZB calls)

## 3. sdf_services — Add reset_state

- [x] 3.1 Add `sdf_services_reset_state()` declaration to `sdf_services.h`
- [x] 3.2 Implement `sdf_services_reset_state()` in `sdf_services.c`:
  - Reset all state variables to defaults
  - Call `led_off()`
- [x] 3.3 Add unit test in `test_sdf_services.c`

## 4. sdf_app — Implement factory reset handler

- [x] 4.1 Modify `sdf_app_on_admin_action()` case `SDF_SERVICES_ADMIN_ACTION_FACTORY_RESET`:
  - Call `sdf_storage_erase_all()`
  - Call `fp_delete_all_users()`
  - Call `sdf_protocol_zigbee_factory_reset()`
  - Call `sdf_services_reset_state()`
  - Call `esp_restart()`
- [x] 4.2 Add integration test in `test_sdf_app.c` (mock all components)

## 5. sdf_cli — Add factory_reset command

- [x] 5.1 Add `factory_reset` command registration in `sdf_cli.c`
- [x] 5.2 Implement command handler with confirmation prompt ("Type 'YES' to confirm")
- [x] 5.3 On confirmation, call same sequence as sdf_app
- [x] 5.4 Add unit test in `test_sdf_cli.c`

## 6. Integration & Validation

- [x] 6.1 Build debug profile: `idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.debug.defaults" build`
- [x] 6.2 Flash and run test_runner on hardware
- [x] 6.3 Test button-hold factory reset (8s hold + admin fingerprint)
- [x] 6.4 Test CLI factory reset command
- [x] 6.5 Verify post-reset state: 0 users, LED breathes WHITE, NVS empty, Zigbee factory-new
- [x] 6.6 Test remote Zigbee Clear All PIN Codes still works after reset

## 7. Documentation Updates

- [x] 7.1 Update `doc/user_manual.md` with factory reset procedure
- [x] 7.2 Update `doc/sdf_sas.md` section 11 (Risks) — mark factory reset as complete
- [x] 7.3 Update `AGENTS.md` version history