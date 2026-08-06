## Context

`sdf_ota` (`components/sdf_ota/src/sdf_ota.c`) already implements a transport-agnostic OTA session core: `sdf_ota_begin(source, image_size, &handle)` opens an `esp_ota_write`-backed session against the next OTA partition, `sdf_ota_write(handle, data, len)` appends bytes, `sdf_ota_verify_integrity()` / `sdf_ota_verify_and_commit()` check the accumulated size and Ed25519 signature (`sdf_ota_signature.c`) before committing. It tracks a single global session tagged with a `sdf_ota_source_t` (`SDF_OTA_SOURCE_ZIGBEE`, `SDF_OTA_SOURCE_CLI`, `SDF_OTA_SOURCE_BLE`), mutex-protected. `sdf_protocol_zigbee.c` already drives this same core from the Zigbee OTA cluster (`sdf_ota_begin(SDF_OTA_SOURCE_ZIGBEE, message->ota_header.image_size, &s_ota_session)`), proving the core needs no transport-specific knowledge.

`sdf_ble_companion_ota.c` currently uses this core too, but only after a WiFi STA join and HTTPS `esp_http_client` download — the WiFi/TLS layer exists purely to get bytes to `sdf_ota_write()`; the door lock has no other use for either. The BLE companion connection (`sdf_ble_companion.c`, shared NimBLE host) already exists for config/enrollment/auth, with an OTA characteristic (`SDF_BLE_COMPANION_OTA_UUID128`), a 512-byte per-connection `ota_value` buffer, and MTU exchange already requested on connect (`ble_gattc_exchange_mtu`), with `CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU=256`.

## Goals / Non-Goals

**Goals:**
- Deliver the firmware image to `sdf_ota_write()` entirely over the existing authenticated BLE GATT connection, with no WiFi/TCP/TLS stack linked in.
- Preserve the existing signed-OTA verification guarantee (Ed25519 signature check) unchanged — the transport swap must not weaken what's verified before commit.
- Support resume/retry: a phone-side BLE disconnect mid-transfer (weak signal, backgrounding, etc.) must not require restarting a multi-hundred-KB transfer from byte 0 every time.
- Keep the transfer inside the same authentication boundary as today (`BLE GATT Authentication` requirement — OTA characteristic remains restricted to authenticated connections).

**Non-Goals:**
- Not designing the web companion app's UI/UX for picking and uploading a firmware file — that's a dependent follow-up in `web-companion/`, out of scope here.
- Not changing `sdf_ota`'s core session/verification logic — this change is purely about how bytes reach `sdf_ota_write()`.
- Not adding WiFi back in any form (softAP, provisioning, etc.) as an alternative transfer path.

## Decisions

- **Framing**: introduce a small begin/chunk/end control scheme on the existing OTA characteristic (or a second characteristic if the GATT database needs a data channel separate from control — see Open Questions):
  - `BEGIN` write carries `{imageSize}` (replaces today's `{ssid, password, firmwareUrl}` JSON), triggering `sdf_ota_begin(SDF_OTA_SOURCE_BLE, imageSize, &handle)`.
  - Subsequent chunk writes are raw binary, each ≤ negotiated ATT MTU minus GATT write overhead (today's buffer is sized for 512 bytes; MTU is requested up to 256 today, so real usable payload per write is smaller — chunk size must be derived from the actual negotiated MTU, not assumed fixed). Each successful chunk write calls `sdf_ota_write()` and is application-ACKed (via notify on the existing notify path, `sdf_ble_companion_notify_ota`) with a running byte offset, so the phone knows what's confirmed-received.
  - `END` write triggers `sdf_ota_verify_integrity()` + `sdf_ota_verify_and_commit()`, with the existing pass/fail JSON status reported back over the same notify channel used today.
  - *Alternative considered*: reuse the existing single 512-byte `ota_value` buffer/characteristic for both control and binary chunks, distinguishing by a 1-byte opcode prefix. Simpler (no new characteristic), likely sufficient — leaning this direction, but flagged as an open question pending a look at NimBLE GATT database capacity/complexity trade-offs during implementation.
- **Resume**: the device reports `bytes_written` (already tracked in `sdf_ota_session_t`) on reconnect within the same OTA attempt; the phone can resume chunk-sending from that offset rather than restarting. If the device reboots or the session times out, the phone must restart from `BEGIN`. No new persistent state is added — this uses the existing in-memory session field, so resume only survives a BLE disconnect/reconnect, not a device reboot.
  - *Alternative considered*: persist partial-OTA progress across reboots (e.g. in NVS). Rejected — adds complexity and a new failure surface (partially-written OTA partitions surviving reboot) for a benefit (surviving a full device reboot mid-firmware-transfer) that doesn't match how OTA is actually triggered (an attended companion-app session).
- **Chunk size**: derive the effective chunk size from the connection's negotiated ATT MTU (already exchanged via `ble_gattc_exchange_mtu` / handled in the `BLE_GAP_EVENT_MTU` case) minus GATT ATT header overhead, rather than a hardcoded constant — avoids either wasting throughput on connections that negotiate a larger MTU or silently truncating writes on connections that don't.
- **Verification unchanged**: `sdf_ota_verify_and_commit()` and Ed25519 signature checking stay exactly as they are today (`sdf_ota_signature.c`) — the transport change does not touch trust boundaries; a compromised/malicious BLE peer still can't push unsigned firmware past this check.

## Risks / Trade-offs

- [Risk] BLE GATT throughput is much lower than WiFi — a multi-hundred-KB firmware image will take meaningfully longer to transfer than the old HTTPS download. → Mitigation: this is an accepted, explicit trade-off (see prior conversation: option (b) chosen with this understood) in exchange for ~500 KB of flash headroom; not something to optimize away in this change.
- [Risk] Extending an OTA transfer across a companion-app background/foreground cycle or phone-side BLE stack quirks could leave the device's OTA session lingering in `SDF_OTA_STATE_DOWNLOADING` indefinitely. → Mitigation: add a bounded idle timeout on an in-progress BLE OTA session (aborting via `sdf_ota_abort()`) so a stalled/abandoned transfer doesn't block future OTA attempts or hold the update partition in a half-written state forever.
- [Risk] Larger, more complex characteristic-handling code in `sdf_ble_companion_ota.c` (framing/reassembly/ack logic) could itself grow larger than the WiFi code it replaces, partially offsetting the size win. → Mitigation: the transport-agnostic `sdf_ota` core does the heavy lifting; the new BLE-side code is expected to be comparable in size to the Zigbee OTA cluster's equivalent glue in `sdf_protocol_zigbee.c`, which is a useful size reference point during implementation.
- [Risk] Existing paired companion apps built against the old `{ssid, password, firmwareUrl}` OTA contract will break. → Mitigation: this is accepted as the **BREAKING** change called out in the proposal; the web companion app needs a coordinated update (tracked as a dependent follow-up, not blocking this firmware-side change from landing).

## Migration Plan

- No on-device data migration — OTA session state is transient/in-memory already.
- Sequencing: land this change's firmware side; the web companion app's OTA flow must be updated in lockstep (or gated) since the wire format changes — coordinate the release so a companion app isn't left pointing at a firmware version that no longer understands `{ssid, password, firmwareUrl}`.
- Rollback: firmware-side rollback is a normal OTA rollback (existing `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` / `sdf_ota_rollback()` machinery, unaffected by this change) — if the BLE transfer path has issues in the field, the prior signed image (still WiFi-OTA-capable, if not yet overwritten) remains bootable via existing rollback.

## Open Questions

- Reuse the existing OTA characteristic for both control messages and binary chunks (opcode-prefixed), or add a dedicated data characteristic? Needs a look at current GATT database attribute count/budget before deciding.
- What idle-timeout value is appropriate for an in-progress BLE OTA session before auto-aborting (mirroring `SDF_BLE_OTA_CONNECT_TIMEOUT_MS` conceptually, but for the whole transfer rather than just WiFi connect)?
- Should progress (bytes confirmed written) be exposed as a readable/notifiable value distinct from the pass/fail completion status the phone already gets today, so a companion app can show a progress bar during a multi-minute BLE transfer?
