## Why

`replace-wifi-ota-with-ble-transfer` changes the firmware's OTA characteristic contract from a `{ssid, password, firmwareUrl}` WiFi-join-and-HTTPS-download request to an opcode-prefixed BEGIN/CHUNK/END chunked binary transfer over the existing BLE GATT connection (see `openspec/specs/ble-companion-service/spec.md`, "OTA Triggering via BLE"). The web companion app (`web-companion/app.js`) still speaks the old contract — it collects a WiFi SSID/password and a firmware URL and writes that JSON to the OTA characteristic. Once firmware built against the new contract ships, the app's OTA flow is incompatible and OTA updates from the web app will fail outright.

## What Changes

- **BREAKING**: Replace the OTA trigger form (`ssid`/`password`/`firmwareUrl` fields, submitted as one JSON write) with a firmware file picker that streams the selected `.bin` over the OTA characteristic using the BEGIN(`0x01`)/CHUNK(`0x02`)/END(`0x03`) opcode framing defined in `ble-companion-service`.
- Add client-side chunking sized to the negotiated ATT MTU (mirroring the firmware's `ble_att_mtu(conn_handle) - 4` sizing), with per-chunk `chunk_ack` offset handling to drive a progress bar.
- Add resume support: on reconnect mid-transfer, re-send `BEGIN` with the same declared image size, read the returned `offset` from the `ready` response, and continue chunk-sending from that byte offset instead of restarting.
- Update the OTA battery-drain warning copy to no longer reference "Wi-Fi" power draw (BLE transfer, not WiFi, is now what's warned about).
- Remove the SSID/password/firmware-URL form fields and any client-side URL/HTTPS validation tied to the old contract.

## Capabilities

### New Capabilities
(none)

### Modified Capabilities
- `web-companion-app`: the "OTA Battery Warning" requirement changes from sending `{ssid, password, firmwareUrl}` to sending a chunked binary firmware transfer via the BEGIN/CHUNK/END opcode contract.

## Impact

- `web-companion/app.js` — OTA form handler (`ota-form` submit listener, `otaChar.writeValue` calls) rewritten for file selection, chunked writes, and offset-based progress/resume.
- `web-companion/index.html` — OTA form markup: replace SSID/password/firmware-URL inputs with a file picker; progress bar wiring may need offset-based (bytes/imageSize) instead of indeterminate updates.
- `openspec/specs/web-companion-app/spec.md` — "OTA Battery Warning" requirement rewritten to describe the new request contract.
- **Release sequencing**: this app-side change is only compatible with firmware built from `replace-wifi-ota-with-ble-transfer` (or later). Deploying the updated web app against older (pre-BLE-OTA) firmware breaks OTA triggering, and shipping BLE-OTA firmware while the deployed web app still speaks the old JSON contract equally breaks OTA triggering. The web app deploy (GitHub Pages) and the firmware release must be coordinated so a device running BLE-OTA firmware is only ever paired against the updated web app, and vice versa — ideally by gating the web app deploy on the corresponding firmware release, or by having the app detect/report a protocol mismatch rather than failing silently.
- No impact on registration, config, or enrollment flows in `web-companion/app.js`, which use unrelated characteristics.
