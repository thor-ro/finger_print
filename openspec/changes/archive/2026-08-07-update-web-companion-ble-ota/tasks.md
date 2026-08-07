## 1. OTA form rework

- [x] 1.1 Replace SSID/password/firmware-URL inputs in `web-companion/index.html`'s OTA form with a firmware file picker (`<input type="file" accept=".bin">`)
- [x] 1.2 Update the OTA battery-drain warning copy in `web-companion/app.js` to describe BLE transfer power draw instead of Wi-Fi

## 2. Chunked transfer implementation

- [x] 2.1 Implement begin-transfer: write opcode `0x01` + 4-byte little-endian image size to the OTA characteristic, await the `ready` notification
- [x] 2.2 Implement chunked send loop: read the firmware file via `File.slice()`, write each chunk as opcode `0x02` + raw bytes, await each `chunk_ack` before sending the next chunk
- [x] 2.3 Implement over-MTU retry: on a rejected chunk write, halve the chunk size and resume sending from the last confirmed `chunk_ack` offset
- [x] 2.4 Implement end-transfer: write opcode `0x03` (no payload) once all bytes are sent
- [x] 2.5 Drive `#ota-progress` from `chunk_ack` offsets (`offset / imageSize`) instead of indeterminate progress
- [x] 2.6 Implement completion inference: after `END`, start a bounded grace timer; a `failed` notification within the window reports the error, a disconnect within the window (no `failed` notification) is treated as presumed success with a "reconnect to confirm" prompt, and neither happening before the timer elapses reports an ambiguous/unknown outcome. **Do not** wait indefinitely for a `success` notification — the firmware's successful-commit path reboots the device before it can send one (confirmed in `sdf_ble_companion_ota.c`'s `sdf_ble_ota_handle_end`), so it never arrives in practice.

## 3. Resume support

- [x] 3.1 On reconnect/re-auth while a transfer was in progress, re-send `BEGIN` with the same declared image size and resume chunk-sending from the returned `offset`

## 4. Verification

- [ ] 4.1 Manual test against BLE-OTA firmware: full transfer success path (device reboots after `END`, no `success` notification arrives), chunk-ack progress updates, and app correctly infers success from the post-`END` disconnect rather than hanging/erroring
- [ ] 4.2 Manual test: disconnect mid-transfer, reconnect within the idle timeout, confirm resume from the reported offset with no duplicate/corrupted bytes
- [ ] 4.3 Manual test: oversized/malformed chunk is rejected by the device without the app corrupting its own send-state (retry/backoff behaves correctly)
- [ ] 4.4 Manual test: signature/integrity verification failure — device sends `failed` notification (does not reboot), app surfaces the error instead of treating it as success

## 5. Release coordination

- [ ] 5.1 Confirm the target device fleet is running BLE-OTA-capable firmware (from `replace-wifi-ota-with-ble-transfer` or later) before deploying this app change to GitHub Pages
- [x] 5.2 Update `web-companion/README.md` / deployment notes to record the firmware version floor this app version requires
