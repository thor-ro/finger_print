## 1. Define Wire Protocol

- [ ] 1.1 Document the config JSON schema: which `sdf_config_t` fields are exposed (LED brightness, sleep timeouts, battery default percent, BLE connect-on-demand flag)
- [ ] 1.2 Document the enroll JSON schema: `{"user_id": N, "permission": P}` request; `{"status": "...", "user_id": N}` response

## 2. Firmware — Config Characteristic

- [ ] 2.1 Implement `sdf_ble_companion_on_config_write()` in `sdf_app.c`: parse JSON payload with cJSON, apply recognized fields via `sdf_config_get_mutable()`, send confirmation notify via `sdf_ble_companion_notify_config()`
- [ ] 2.2 Implement config read: when Config characteristic is read (in `sdf_ble_companion_config_access` READ path), serialize current `sdf_config_t` subset to JSON and return it
- [ ] 2.3 Add MTU negotiation request after GATT connect in `sdf_ble_companion_gap_event()` (call `ble_att_set_preferred_mtu(512)` or `ble_gattc_exchange_mtu()`)

## 3. Firmware — Enroll Characteristic

- [ ] 3.1 Implement `sdf_ble_companion_on_enroll_write()` in `sdf_app.c`: parse JSON, validate user_id and permission, call `sdf_services_request_enrollment()`
- [ ] 3.2 Subscribe to `SDF_EVENT_ROUTER_ENROLLMENT_COMPLETE` and `SDF_EVENT_ROUTER_ENROLLMENT_FAILED` in the BLE companion init path to send result notifications
- [ ] 3.3 When enrollment completes or fails, call `sdf_ble_companion_notify_enroll()` with a JSON status payload

## 4. Web Companion

- [ ] 4.1 In `app.js`, add event listener for `#btn-read-config`: get Config characteristic, read it, parse JSON, display fields
- [ ] 4.2 Add MTU exchange request in the connect flow (`requestMTU` or equivalent Web Bluetooth API)
- [ ] 4.3 Add enrollment UI to `index.html` dashboard: user_id input, permission select, "Enroll Fingerprint" button
- [ ] 4.4 Implement enroll handler in `app.js`: get Enroll characteristic, write JSON payload, subscribe to enroll notifications, show progress

## 5. Build & Verify

- [ ] 5.1 Build firmware, confirm no compile errors
- [ ] 5.2 Verify config read returns valid JSON in browser console
- [ ] 5.3 Verify enrollment trigger calls `sdf_services_request_enrollment()` via log output
