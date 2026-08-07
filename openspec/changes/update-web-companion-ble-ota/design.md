## Context

`web-companion/app.js` currently triggers OTA by writing a single `{ssid, password, firmwareUrl}` JSON payload to the OTA characteristic and waiting for a pass/fail notification, per the (now superseded) `web-companion-app` "OTA Battery Warning" requirement. `replace-wifi-ota-with-ble-transfer` replaced the device-side contract with an opcode-prefixed BEGIN(`0x01`)/CHUNK(`0x02`)/END(`0x03`) chunked binary transfer (`openspec/specs/ble-companion-service/spec.md`), acknowledged per-chunk via `chunk_ack` notifications carrying a confirmed byte offset, with resume support keyed on the device reporting `bytes_written` when a client re-authenticates mid-transfer. This document covers the app-side implementation to speak that new contract.

## Goals / Non-Goals

**Goals:**
- Replace the SSID/password/firmware-URL form with a firmware file picker that streams the file directly to the device over the existing BLE GATT connection, following the BEGIN/CHUNK/END framing.
- Size each chunk write to the connection's negotiated ATT MTU (mirroring the firmware's `mtu - 4` sizing) so writes are never rejected as over-MTU.
- Drive a byte-accurate progress bar from `chunk_ack` offsets instead of the current indeterminate progress display.
- Support resuming a transfer after a BLE disconnect/reconnect within the device's 60s idle-timeout window, per the "Resume after disconnect" scenario.
- Keep this change confined to the OTA flow in `web-companion/app.js` / `index.html`; registration, config, and enrollment flows are unaffected.

**Non-Goals:**
- Not adding firmware image signing/packaging tooling to the web app — the app streams whatever `.bin` file the user selects; signature verification remains entirely device-side (`sdf_ota_verify_and_commit`), unchanged by this app-side work.
- Not building a firmware-version compatibility check or an update catalog/download source in the app — file selection is local (user supplies the `.bin`), same as today's manual firmware-URL entry was user-supplied.
- Not solving cross-repo release automation — the coordination note below is a process/sequencing concern, not a technical dependency to build.

## Decisions

- **Chunk size derivation**: read `BluetoothRemoteGATTCharacteristic`'s effective write size isn't directly exposed by Web Bluetooth, so the app uses a conservative fixed chunk size (`180` bytes payload, chosen safely below the smallest commonly-negotiated ATT MTU of ~185 on default browser/OS BLE stacks) rather than attempting to query the negotiated MTU from the browser, which the Web Bluetooth API does not expose. If a chunk write is rejected by the device (over-MTU), the app halves the chunk size and retries from the last confirmed offset. *Alternative considered*: assume `CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU=256` and use `256 - 4`; rejected because the negotiated MTU depends on the central (browser/OS) and can be smaller, and Web Bluetooth gives no reliable way to read it back.
- **Resume trigger**: on `characteristicvaluechanged`/reconnect after a disconnect during an active transfer, the app re-sends `BEGIN` with the previously-declared image size before resuming chunk writes, per the device's resume contract (matching declared size on an already-open session = resume, no data loss).
- **Progress UI**: reuse the existing `#ota-progress` `<progress>` element, now driven by `offset / imageSize` from `chunk_ack` payloads instead of being set indeterminately.
- **END has no reachable success notify — confirmed against the actual firmware source** (`sdf_ble_companion_ota.c`'s `sdf_ble_ota_handle_end`): on successful verification, `sdf_ota_verify_and_commit()` reboots the device immediately and never returns, so the `{"status":"success"}` notify is unreachable in practice — the client only ever sees a BLE disconnect right after the `END` write is ATT-acknowledged. The `{"status":"failed","error":...}` notify **does** reliably arrive on a verification/commit failure (the device does not reboot in that path). Consequently the app must NOT treat "END sent, no success notify received, connection dropped" as an error — that is the expected successful path. UI flow: after writing `END`, show "Verifying and installing — the device will restart" and start a short grace timer (a few seconds); if a `failed` notify arrives within that window, show the error; if the connection instead drops (or nothing arrives) within the window, treat it as a presumed success and prompt the user to reconnect once the device comes back up to confirm the new firmware version. Only surface a hard "unknown outcome, please check the device" error if neither a `failed` notify nor a disconnect happens within the grace window (e.g. connection stays open with silence — genuinely ambiguous, worth surfacing rather than guessing).

## Risks / Trade-offs

- [Risk] A fixed conservative chunk size under-uses connections that negotiate a larger MTU, slowing an already-slow BLE transfer further. → Mitigation: accepted trade-off for correctness over the unknown-MTU-from-browser constraint; can be revisited if Web Bluetooth exposes negotiated MTU in the future.
- [Risk] Large firmware files (multi-hundred KB) held in browser memory and chunked in a tight write loop could be slow or janky on low-end devices/browsers. → Mitigation: use `File.slice()` to read chunks lazily rather than loading the whole file into one buffer up front.
- [Risk] Users on old cached versions of the static site (GitHub Pages, browser cache) could still submit the old WiFi-credentials JSON against BLE-OTA-only firmware, silently failing. → Mitigation: no app-side fix possible for stale caches; covered by the release-sequencing note in the proposal (cache-busting / versioned deploy is a process concern, not blocking this change).

## Migration Plan

- No server/data migration — this is a static-asset-only app change.
- Sequencing: this app change must not be deployed to GitHub Pages ahead of the corresponding BLE-OTA firmware release reaching devices in the field the app is expected to manage, and vice versa (see proposal's Impact section). No automated gate exists today; deploy order is a manual coordination step until/unless a compatibility check is added.
- Rollback: a normal static-asset redeploy of the previous `web-companion/` version; no persistent app-side state to roll back.
