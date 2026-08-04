## 1. Add Broadcast Helper to sdf_ble_companion

- [ ] 1.1 Add `sdf_ble_companion_broadcast_ota(const uint8_t *data, size_t len)` to `sdf_ble_companion.c` and its header — iterates `s_connections[]` under the mutex and calls `ble_gatts_notify_custom()` for each authenticated connection
- [ ] 1.2 Add a helper `sdf_ble_ota_notify(const char *json)` in `sdf_ble_companion_ota.c` that encodes a JSON string and calls the broadcast function

## 2. Add Notify Calls in OTA Task

- [ ] 2.1 After WiFi init begins: notify `{"status":"wifi_connecting"}`
- [ ] 2.2 After WiFi connected: notify `{"status":"wifi_connected"}`
- [ ] 2.3 After HTTP headers received and content_length known: notify `{"status":"downloading","progress":0}`
- [ ] 2.4 In the download loop: track bytes read, notify `{"status":"downloading","progress":N}` at 25%, 50%, 75% boundaries
- [ ] 2.5 After verify pass: notify `{"status":"verifying"}`
- [ ] 2.6 On commit success: notify `{"status":"success"}`
- [ ] 2.7 On any failure: notify `{"status":"failed","error":"<reason>"}`

## 3. Update Web Companion

- [ ] 3.1 In `app.js` OTA form submit handler: get OTA characteristic, subscribe to notifications before writing the trigger payload
- [ ] 3.2 Add `handleOtaNotification(event)` handler: parse JSON, update progress bar and status text
- [ ] 3.3 In `index.html`, add a `<progress>` element inside the OTA section, initially hidden, shown when download begins

## 4. Build & Verify

- [ ] 4.1 Build firmware, confirm no compile errors
- [ ] 4.2 Trigger OTA via web companion and confirm status messages arrive in browser console
