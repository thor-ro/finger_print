## Why

All four GATT characteristics in `sdf_ble_companion.c` are declared with the `BLE_GATT_CHR_F_INDICATE` flag, but the notify helper functions call `ble_gatts_notify_custom()`. Notifications and indications are distinct ATT operations with different delivery semantics: notifications are fire-and-forget, while indications require an acknowledgement. The web companion calls `authChar.startNotifications()`, which subscribes to ATT notifications (CCCD bit 0). If the device sends an indication (CCCD bit 1), the JS event handler never fires — the authentication result is silently dropped.

## What Changes

- Change all four characteristics from `BLE_GATT_CHR_F_INDICATE` to `BLE_GATT_CHR_F_NOTIFY`
- Keep `sdf_ble_companion_notify_*` functions using `ble_gatts_notify_custom()` (correct for notifications)
- Update `sdf_ble_companion_set_authenticated()` to use `ble_gatts_notify_custom()` instead of `ble_gatts_indicate_custom()`
- Alternatively: if reliable delivery for auth results is desired, keep INDICATE on the auth characteristic and update the web companion to use `startNotifications()` in indicate mode — document the choice

## Capabilities

### New Capabilities
- None

### Modified Capabilities
- None (protocol correctness fix)

## Impact

- `firmware/components/sdf_ble_companion/src/sdf_ble_companion.c` — characteristic flags, `set_authenticated()`
- `web-companion/app.js` — verify `startNotifications()` aligns with device characteristic type
- Web clients already using the service will now reliably receive auth results
