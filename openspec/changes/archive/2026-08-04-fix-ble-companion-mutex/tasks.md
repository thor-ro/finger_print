## 1. Audit All Shared State Accesses

- [x] 1.1 Identify every read/write of `s_connections[]` in `sdf_ble_companion.c` and note which task context it runs in
- [x] 1.2 Identify every read/write of `s_auth_value`, `s_config_value`, `s_enroll_value`, `s_ota_value` buffers

## 2. Add Mutex Guards — GATT Access Callbacks (NimBLE task)

- [x] 2.1 In `sdf_ble_companion_auth_access()`: take `s_lock` with a 10ms timeout at entry, release before returning; log a warning on contention
- [x] 2.2 In `sdf_ble_companion_config_access()`: same pattern
- [x] 2.3 In `sdf_ble_companion_enroll_access()`: same pattern
- [x] 2.4 In `sdf_ble_companion_ota_access()`: same pattern
- [x] 2.5 In `sdf_ble_companion_gap_event()` (CONNECT/DISCONNECT cases): take lock with 10ms timeout when reading/writing `s_connections[]`

## 3. Add Mutex Guards — Public API (App task)

- [x] 3.1 In `sdf_ble_companion_set_authenticated()`: lock → update state → unlock → call `ble_gatts_notify_custom()` outside lock
- [x] 3.2 In `sdf_ble_companion_reply_auth()`: lock → find conn → copy handle → unlock → call `set_authenticated()`
- [x] 3.3 In `sdf_ble_companion_notify_config/enroll/ota()`: lock → validate conn + copy buffer → unlock → call `ble_gatts_notify_custom()` outside lock
- [x] 3.4 In `sdf_ble_companion_is_authenticated()`: lock → read state → unlock → return value

## 4. Verify

- [x] 4.1 Build firmware, confirm no compile errors
- [x] 4.2 Verify mutex is never held when calling any `ble_gatts_*` or `ble_gap_*` function
