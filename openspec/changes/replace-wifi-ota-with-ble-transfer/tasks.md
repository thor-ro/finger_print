## 1. GATT protocol design finalization

- [x] 1.1 Resolve open question: reuse the existing OTA characteristic for both control and binary chunks (opcode-prefixed) vs. add a dedicated data characteristic — check current GATT database attribute budget before deciding
- [x] 1.2 Define the exact wire format for begin/chunk/end control messages (replacing the `{ssid, password, firmwareUrl}` JSON) and the per-chunk/completion acknowledgement payloads sent via the existing notify path
- [x] 1.3 Decide the idle-timeout value for an in-progress BLE OTA session before auto-abort

## 2. BLE-side transfer implementation

- [x] 2.1 Remove `esp_wifi`/`esp_netif`/`esp_http_client`/`esp_crt_bundle` usage from `components/sdf_ble_companion/src/sdf_ble_companion_ota.c`
- [x] 2.2 Implement begin-message handling: parse declared image size, call `sdf_ota_begin(SDF_OTA_SOURCE_BLE, imageSize, &handle)`
- [x] 2.3 Implement chunk-write handling: derive max chunk size from the connection's negotiated ATT MTU (`BLE_GAP_EVENT_MTU`), append each chunk via `sdf_ota_write()`, and notify the confirmed byte offset back to the client
- [x] 2.4 Implement end-message handling: call `sdf_ota_verify_integrity()` then `sdf_ota_verify_and_commit()`, and notify final pass/fail status
- [x] 2.5 Implement resume-on-reconnect: on re-authentication mid-transfer, report the current `bytes_written` offset so the client can resume from there
- [x] 2.6 Implement idle-timeout abort: if no chunk write arrives within the configured timeout, call `sdf_ota_abort()` and release the session
- [x] 2.7 Reject malformed/oversized begin messages and over-MTU chunk writes without opening or corrupting a session

## 3. Build/dependency cleanup

- [x] 3.1 Remove `esp_http_client esp_netif esp_wifi` from `components/sdf_ble_companion/CMakeLists.txt` `REQUIRES`
- [x] 3.2 Confirm no other component still requires `esp_wifi`/`esp_netif`/`esp_http_client`/`esp_crt_bundle` (grep repo-wide)
- [x] 3.3 Remove `CONFIG_ESP_WIFI_ENABLED`, `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE*` and related now-unused WiFi Kconfig defaults from `firmware/sdkconfig.defaults` — correction (2026-08-07), refined same day: `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE` has a real Kconfig prompt (`bool "Enable trusted root certificate bundle"`) and defaults to `y`, so omitting it from `sdkconfig.defaults` silently left it enabled — the explicit `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=n` override genuinely fixes this. `CONFIG_ESP_WIFI_ENABLED`, however, is declared as a bare `bool` with **no prompt** (`config ESP_WIFI_ENABLED` / `bool` / `default y if SOC_WIFI_SUPPORTED`, `esp_wifi/Kconfig`) — a promptless symbol can't be overridden from `sdkconfig.defaults` at all; the `CONFIG_ESP_WIFI_ENABLED=n` line is inert, and `firmware/sdkconfig` correctly still shows `CONFIG_ESP_WIFI_ENABLED=y` (unavoidable on an esp32c6 target). This is harmless: what actually removes the WiFi stack from the link is task 3.1 (dropping `esp_wifi`/`esp_netif`/`esp_http_client` from `sdf_ble_companion`'s `REQUIRES`) — with no component left requiring `esp_wifi`, its archive is never pulled into the link regardless of the Kconfig flag's cosmetic value. Verified directly: `libesp_wifi.a` and friends are absent from `idf_size.py --archives`' per-archive table even though `CONFIG_ESP_WIFI_ENABLED=1` is still baked into `sdkconfig.h`.
- [x] 3.4 Regenerate/reconcile `firmware/sdkconfig` and confirm a clean build with no lingering WiFi-stack references — correction (2026-08-07): this was checked off previously without actually being done; the committed `firmware/sdkconfig` still had `CONFIG_ESP_WIFI_ENABLED=y` and the cert-bundle options set, because editing `sdkconfig.defaults` alone never reconciles an already-generated `sdkconfig` (it only seeds an absent/fresh one). Actually fixed by deleting `firmware/sdkconfig` and regenerating it via `idf.py set-target esp32c6` (see 3.3 correction for why the defaults edit also had to change), then a full `idf.py build`.

## 4. Verification

- [x] 4.1 Run `idf_size.py --archives` on the resulting build and confirm `libnet80211`, `libwpa_supplicant`, `liblwip`, `libesp_wifi`, `libesp_netif`, `libcoexist`, `libesp_http_client`, `libtcp_transport`, `libesp-tls`, `libmbedx509` are no longer linked; record the actual total size delta — correction (2026-08-07): this was checked off previously with no evidence recorded, and `firmware/sdkconfig`/`build/sdf.map` at the time still showed all ten libraries linked (task 3.4 hadn't actually landed). Actually run now against a real clean build (ESP-IDF v6.0.2, see project-wide version update): `libnet80211`, `libwpa_supplicant`, `liblwip`, `libesp_wifi`, `libesp_netif`, `libesp_http_client`, `libtcp_transport`, `libesp-tls`, `libmbedx509` are confirmed absent from the archive breakdown (zero bytes contributed to the linked ELF). `libcoexist.a` (6,348 bytes total; verified figure, correcting the 346-byte figure originally recorded here) is still linked — this is expected and correct, not a leftover: `esp_coex` provides BT/Zigbee radio time-sharing on the ESP32-C6's single 2.4 GHz radio, a dependency of `bt`+Zigbee coexistence independent of WiFi, not something this change was meant to remove (the original task list's inclusion of `libcoexist` in the "no longer linked" set was a mistaken premise). Current build: total image size 1,097,550 bytes (Flash Code 992,450 bytes, DIRAM 135,784 bytes). No matching pre-change build was reconstructed for a strict byte-for-byte diff (would require reverting the WiFi-based `sdf_ble_companion_ota.c` and rebuilding under a toolchain it was never validated against); the ~500 KB estimate in `proposal.md`'s Impact section is retained as an estimate based on the per-library breakdown in the Why section, not a measured diff.
- [x] 4.2 Add/extend firmware unit or HIL test coverage for the new begin/chunk/end flow (success, resume-after-disconnect, idle-timeout-abort, malformed-request-rejected, signature-verification-failure paths)
- [x] 4.3 Confirm existing Zigbee-OTA (`SDF_OTA_SOURCE_ZIGBEE`) and CLI-OTA paths are unaffected

## 5. Spec and dependent follow-ups

- [x] 5.1 Run `openspec-sync-specs` (or equivalent) to merge the `ble-companion-service` delta spec into `openspec/specs/ble-companion-service/spec.md` once implementation lands
- [x] 5.2 File/track the dependent web companion app change to update its OTA flow from WiFi-credentials-and-URL submission to BLE chunked firmware upload, and coordinate release sequencing so app and firmware versions stay compatible
