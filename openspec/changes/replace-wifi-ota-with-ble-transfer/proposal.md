## Why

The ESP32-C6 app partition has only 93.5 KB (4.8%) free in `ota_1`. A size breakdown of `build/sdf.map` (`idf_size.py --archives`) shows the WiFi/HTTP/TLS-session stack — `libnet80211` (193 KB), `libwpa_supplicant` (74 KB), `liblwip` (101 KB), `libesp_wifi`, `libesp_netif`, `libcoexist`, `libesp_http_client`, `libtcp_transport`, `libesp-tls`, `libmbedx509` (combined ~55 KB more), plus the mbedtls default-full certificate bundle (~68 KB rodata, `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_FULL`) and the TLS handshake state machine inside `libmbedtls.a` — sums to roughly 500+ KB (over a quarter of the binary). Grepping the codebase confirms every one of these is linked in *solely* for one feature: `sdf_ble_companion_ota.c` joining a WiFi network (credentials supplied over BLE) and downloading the firmware image over HTTPS. Nothing else touches `esp_wifi`, `esp_netif`, `esp_http_client`, or `esp_crt_bundle`.

The device already has an authenticated, encrypted BLE GATT link to the companion app for config/enrollment/auth (`sdf_ble_companion`, on the shared NimBLE host `sdf_protocol_ble` already pays for). Delivering the firmware image over that same link — instead of standing up a second radio stack (WiFi STA + full TCP/IP + TLS) just for this one transfer — removes the largest single contributor to flash pressure without adding a new dependency.

## What Changes

- **BREAKING**: Remove the WiFi-join-and-HTTPS-download OTA path entirely: `sdf_ble_companion_ota.c`'s `esp_wifi`/`esp_netif`/`esp_http_client`/`esp_crt_bundle` usage, the `{ssid, password, firmwareUrl}` JSON request format, and the `esp_wifi`/`esp_netif`/`esp_http_client`/`mbedtls certificate bundle` build dependencies (`sdf_ble_companion`'s `REQUIRES`).
- Add a BLE GATT chunked binary transfer for OTA: the companion app streams the (already Ed25519-signed) firmware image directly over the existing OTA characteristic/connection as a sequence of writes, sized to the negotiated ATT MTU, with a begin/chunk/complete framing that supports resume/retry on mid-transfer disconnect.
- Wire the chunked writes directly into the existing transport-agnostic OTA core (`sdf_ota_begin(SDF_OTA_SOURCE_BLE, ...)` / `sdf_ota_write()` / `sdf_ota_verify_and_commit()` in `components/sdf_ota`) — this core, and its signature verification, do not change; `SDF_OTA_SOURCE_BLE` already exists as an enum value, currently unused. `SDF_OTA_SOURCE_ZIGBEE`'s existing Zigbee-OTA-cluster consumer (`sdf_protocol_zigbee.c`) is an in-repo precedent for feeding this same core from a non-WiFi transport.
- Update `ble-companion-service` spec's "OTA Triggering via BLE" requirement to describe the new chunked-transfer request/response contract instead of the WiFi-credentials-and-URL contract.
- Remove `CONFIG_ESP_WIFI_ENABLED`, `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE*`, and related WiFi Kconfig defaults from `sdkconfig.defaults` (kept only if some other still-undiscovered consumer needs them — to be confirmed during design).

## Capabilities

### New Capabilities
(none)

### Modified Capabilities
- `ble-companion-service`: the "OTA Triggering via BLE" requirement changes from `{ssid, password, firmwareUrl}` + WiFi/HTTPS fetch to a chunked binary transfer over the existing GATT connection, still gated by BLE GATT Authentication and still validated through the existing signed OTA verification flow.

## Impact

- `components/sdf_ble_companion/src/sdf_ble_companion_ota.c` — WiFi/HTTP fetch logic removed, replaced with chunk-receive/reassembly logic feeding `sdf_ota_write()`.
- `components/sdf_ble_companion/CMakeLists.txt` — drop `esp_http_client esp_netif esp_wifi` from `REQUIRES`.
- `components/sdf_ota` — no functional change expected; `SDF_OTA_SOURCE_BLE` moves from defined-but-unused to actually exercised.
- `firmware/sdkconfig*.defaults` — remove WiFi/cert-bundle config once confirmed unused elsewhere.
- `openspec/specs/ble-companion-service/spec.md` — "OTA Triggering via BLE" requirement rewritten.
- Web companion app (`web-companion/`) — its OTA trigger UI/flow needs to switch from submitting WiFi credentials + a firmware URL to uploading/streaming a firmware binary over the BLE connection (out of scope for firmware-side tasks, but a dependent follow-up).
- Flash savings (confirmed 2026-08-07, ESP-IDF v6.0.2, clean build): `idf_size.py --archives` on `build/sdf.map` confirms `libnet80211`, `libwpa_supplicant`, `liblwip`, `libesp_wifi`, `libesp_netif`, `libesp_http_client`, `libtcp_transport`, `libesp-tls`, `libmbedx509` are no longer linked (zero bytes contributed to the ELF). `libcoexist.a` remains linked at 346 bytes — this is expected: it is the BT/Zigbee radio coexistence scheduler on the shared 2.4 GHz radio, unrelated to WiFi/OTA and out of scope for removal. Current build totals 1,097,550 bytes (Flash Code 992,450 bytes, DIRAM 135,784 bytes). No byte-for-byte pre/post diff was reconstructed (would require reverting the WiFi-based `sdf_ble_companion_ota.c` and rebuilding under a toolchain it predates); the original ~500 KB estimate above (per-library breakdown in the Why section: `libnet80211` 193 KB + `libwpa_supplicant` 74 KB + `liblwip` 101 KB + ~55 KB more + ~68 KB cert-bundle rodata) remains the best available estimate of savings and is consistent with all of those libraries now being confirmed absent from the link.
- No impact on Zigbee OTA (`SDF_OTA_SOURCE_ZIGBEE`) or CLI-triggered OTA paths, which are unaffected by this change.
