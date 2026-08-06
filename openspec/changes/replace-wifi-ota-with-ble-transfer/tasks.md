## 1. GATT protocol design finalization

- [ ] 1.1 Resolve open question: reuse the existing OTA characteristic for both control and binary chunks (opcode-prefixed) vs. add a dedicated data characteristic — check current GATT database attribute budget before deciding
- [ ] 1.2 Define the exact wire format for begin/chunk/end control messages (replacing the `{ssid, password, firmwareUrl}` JSON) and the per-chunk/completion acknowledgement payloads sent via the existing notify path
- [ ] 1.3 Decide the idle-timeout value for an in-progress BLE OTA session before auto-abort

## 2. BLE-side transfer implementation

- [ ] 2.1 Remove `esp_wifi`/`esp_netif`/`esp_http_client`/`esp_crt_bundle` usage from `components/sdf_ble_companion/src/sdf_ble_companion_ota.c`
- [ ] 2.2 Implement begin-message handling: parse declared image size, call `sdf_ota_begin(SDF_OTA_SOURCE_BLE, imageSize, &handle)`
- [ ] 2.3 Implement chunk-write handling: derive max chunk size from the connection's negotiated ATT MTU (`BLE_GAP_EVENT_MTU`), append each chunk via `sdf_ota_write()`, and notify the confirmed byte offset back to the client
- [ ] 2.4 Implement end-message handling: call `sdf_ota_verify_integrity()` then `sdf_ota_verify_and_commit()`, and notify final pass/fail status
- [ ] 2.5 Implement resume-on-reconnect: on re-authentication mid-transfer, report the current `bytes_written` offset so the client can resume from there
- [ ] 2.6 Implement idle-timeout abort: if no chunk write arrives within the configured timeout, call `sdf_ota_abort()` and release the session
- [ ] 2.7 Reject malformed/oversized begin messages and over-MTU chunk writes without opening or corrupting a session

## 3. Build/dependency cleanup

- [ ] 3.1 Remove `esp_http_client esp_netif esp_wifi` from `components/sdf_ble_companion/CMakeLists.txt` `REQUIRES`
- [ ] 3.2 Confirm no other component still requires `esp_wifi`/`esp_netif`/`esp_http_client`/`esp_crt_bundle` (grep repo-wide)
- [ ] 3.3 Remove `CONFIG_ESP_WIFI_ENABLED`, `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE*` and related now-unused WiFi Kconfig defaults from `firmware/sdkconfig.defaults`
- [ ] 3.4 Regenerate/reconcile `firmware/sdkconfig` and confirm a clean build with no lingering WiFi-stack references

## 4. Verification

- [ ] 4.1 Run `idf_size.py --archives` on the resulting build and confirm `libnet80211`, `libwpa_supplicant`, `liblwip`, `libesp_wifi`, `libesp_netif`, `libcoexist`, `libesp_http_client`, `libtcp_transport`, `libesp-tls`, `libmbedx509` are no longer linked; record the actual total size delta
- [ ] 4.2 Add/extend firmware unit or HIL test coverage for the new begin/chunk/end flow (success, resume-after-disconnect, idle-timeout-abort, malformed-request-rejected, signature-verification-failure paths)
- [ ] 4.3 Confirm existing Zigbee-OTA (`SDF_OTA_SOURCE_ZIGBEE`) and CLI-OTA paths are unaffected

## 5. Spec and dependent follow-ups

- [ ] 5.1 Run `openspec-sync-specs` (or equivalent) to merge the `ble-companion-service` delta spec into `openspec/specs/ble-companion-service/spec.md` once implementation lands
- [ ] 5.2 File/track the dependent web companion app change to update its OTA flow from WiFi-credentials-and-URL submission to BLE chunked firmware upload, and coordinate release sequencing so app and firmware versions stay compatible
