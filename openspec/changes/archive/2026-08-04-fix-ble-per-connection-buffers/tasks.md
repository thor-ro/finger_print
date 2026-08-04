## 1. Update Connection Struct

- [ ] 1.1 In `sdf_ble_companion.h`, add four value buffer fields to `sdf_ble_companion_connection_t`: `auth_value[512]`, `auth_value_len`, `config_value[512]`, `config_value_len`, `enroll_value[512]`, `enroll_value_len`, `ota_value[512]`, `ota_value_len`
- [ ] 1.2 In `sdf_ble_companion.c`, remove the four global `s_*_value` arrays and their `s_*_value_len` variables

## 2. Update Access Callbacks

- [ ] 2.1 In `sdf_ble_companion_auth_access()`: replace `s_auth_value` / `s_auth_value_len` with `conn->auth_value` / `conn->auth_value_len`
- [ ] 2.2 In `sdf_ble_companion_config_access()`: same for config buffer
- [ ] 2.3 In `sdf_ble_companion_enroll_access()`: same for enroll buffer
- [ ] 2.4 In `sdf_ble_companion_ota_access()`: same for OTA buffer

## 3. Update Notify and Reply Functions

- [ ] 3.1 In `sdf_ble_companion_set_authenticated()`: update to use `conn->auth_value` / `conn->auth_value_len`
- [ ] 3.2 In `sdf_ble_companion_notify_config()`: update to use `conn->config_value` / `conn->config_value_len`
- [ ] 3.3 In `sdf_ble_companion_notify_enroll()`: update for enroll buffer
- [ ] 3.4 In `sdf_ble_companion_notify_ota()`: update for OTA buffer

## 4. Connection Init

- [ ] 4.1 In the `BLE_GAP_EVENT_CONNECT` handler and `memset(s_connections, 0, ...)` in `init()`: confirm zero-init covers the new fields (it will, via `memset`)

## 5. Build & Verify

- [ ] 5.1 Build firmware, confirm no compile errors
- [ ] 5.2 Check memory: run `idf.py size-components` and confirm `.data` / heap usage is acceptable (expect ~4KB increase in BSS for connection structs)
