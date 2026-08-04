## 1. Fix Characteristic Flags

- [ ] 1.1 In `sdf_ble_companion.c`, change all four characteristic definitions from `BLE_GATT_CHR_F_INDICATE` to `BLE_GATT_CHR_F_NOTIFY`
- [ ] 1.2 In `sdf_ble_companion_set_authenticated()`, change `ble_gatts_indicate_custom()` to `ble_gatts_notify_custom()`

## 2. Verify Web Companion Alignment

- [ ] 2.1 Confirm `web-companion/app.js` calls `authChar.startNotifications()` — correct for NOTIFY characteristics
- [ ] 2.2 Verify the `characteristicvaluechanged` handler is registered before `startNotifications()` (already correct in current code)

## 3. Build & Verify

- [ ] 3.1 Build firmware, confirm no compile errors
- [ ] 3.2 Manually verify (or note for hardware test): after login, web companion receives the `0x01` auth-ok notification
