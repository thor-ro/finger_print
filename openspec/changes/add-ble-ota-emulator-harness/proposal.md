## Why

`adopt-idf-signed-app-ota` deletes the host-side ECDSA known-answer-vector tests and the streaming-digest tests, because verification moves into `bootloader_support`, which is not built for `IDF_TARGET=linux`. Its design decision D8 designated an automated `esp-emu` run as the replacement CI gate. That gate was never built: tasks 7.1–7.4 and 8.4–8.5 remain open. The result is that **nothing in CI currently proves OTA signature verification is active**. A regression that silently disables it — a dropped Kconfig symbol, a swapped `esp_ota_end()` call, a reintroduced fail-open branch — would produce a green build and a device that accepts unsigned firmware.

The harness was previously assessed as infeasible because it needs a bonded, encrypted GATT central speaking the companion wire protocol. Investigation shows otherwise on every count:

- `esp-emu` v0.39.0 ships an HCI backend for exactly this: `--ble-hci "tcp:host:port"` documented as a Bumble transport, plus `--elf` for BLE symbol interception.
- Pairing is LE Secure Connections **Just Works** — `sdf_nuki_ble_transport.c:728-733` sets `sm_io_cap = BLE_SM_IO_CAP_NO_IO`, `sm_mitm = 0`, `sm_sc = 1`. No passkey or OOB handling is needed.
- The application-layer login is PBKDF2-HMAC-SHA256 credential stretching plus an HMAC-SHA256 challenge response (`sdf_services_web_auth.c:36, 47-55`), reimplementable in Python with `hashlib` alone.
- A fixture app can call the existing public `sdf_ble_companion_open_pairing_window()` at boot so the device advertises with `BLE_HCI_ADV_FILT_NONE` (`sdf_ble_companion.c:1173-1179`) and an unbonded central can connect.

What remains genuinely unproven is whether `esp-emu`'s BLE support carries a full pairing plus GATT notification flow, as opposed to intercepting controller symbols only. That is the one real risk, and it is why this change is layered.

## What Changes

The security gate the parent change needs does **not** require BLE. Feeding an image over the companion transport exercises the transport; rejecting a tampered image exercises the signature. Separating them lets the gate land without waiting on the unproven part:

- **Layer 1 — transport-independent signature gate.** Add an on-target fixture, run under `esp-emu` on `esp32c6`, that drives `sdf_ota_begin()` / `sdf_ota_write()` / `sdf_ota_verify_and_commit()` over three pre-staged images and asserts: correctly signed commits; signed-then-byte-flipped is rejected; signed with a different key is rejected. Images are pre-staged into a source partition in the merged flash image rather than transferred, so no transport is involved. This alone closes the `adopt-idf-signed-app-ota` D8 gap.
- **Layer 2 — BLE GATT central harness.** Add a Bumble-based Python central that connects to the emulated device over `--ble-hci tcp:`, pairs with LE SC Just Works, registers and logs in against the companion AUTH characteristic, subscribes to OTA notifications, and drives `BEGIN`/`CHUNK`/`END` (`sdf_ble_ota_protocol.h:19-21`) across the same three fixtures — asserting the JSON status notifications (`{"status":"chunk_ack"}`, `{"status":"success"}`, `{"status":"failed","error":...}`) and that a rejected session leaves the device able to start a subsequent OTA.
- Add fixture generation producing the three signed artifacts from one build, all signed against the running image's key so the trust anchor (the running app's own signature block) is exercised as it behaves in the field.
- Add the CI job `ota-signature (esp32c6 — esp-emu)` that `adopt-idf-signed-app-ota` specifies but does not implement, running Layer 1 as a required check and Layer 2 as a required check once it is proven.
- Reserve a documented fallback: if `esp-emu` BLE proves unable to carry pairing plus GATT, Layer 2 is dropped to hardware-only and Layer 1 remains the CI gate. The security property stays covered either way.

## Capabilities

### New Capabilities

- `ota-signature-emulator-gate`: An automated, hardware-free check that a tampered or wrong-key image is rejected at commit and a correctly signed one is accepted, replacing the deleted host crypto tests as the CI gate on signature verification.
- `ble-ota-emulator-harness`: A scriptable GATT central that drives the companion BLE OTA wire protocol against an emulated device, covering pairing, login, chunked transfer, status notification and session recovery without hardware.

### Modified Capabilities

None. `firmware-ci`'s third-job requirement and `firmware-host-test-runner`'s narrowed coverage are already introduced by `adopt-idf-signed-app-ota`'s spec deltas; this change implements them rather than restating them.

## Impact

**Dependency ordering**
- This change depends on `adopt-idf-signed-app-ota` landing first — it verifies that change's mechanism and consumes its signing key and signed-image format.
- `adopt-idf-signed-app-ota` tasks 7.1–7.4, 8.4 and 8.5 are superseded by this change and should be annotated as such rather than silently dropped, so the parent's own record shows where its gate went. Its hardware tasks 6.2–6.7 are unaffected and remain the release check.

**New**
- A fixture app and its build configuration for the `esp32c6` target, built out-of-tree following the `AGENTS.md` emulator pattern so the in-tree `linux` sdkconfig and build stay untouched.
- A Python harness package with a Bumble dependency, and a fixture-generation step producing the three signed images.

**CI**
- `.github/workflows/firmware-ci.yml` gains the third job, in the `espressif/idf:v6.0.2` container with `esp-emu` and Bumble installed. It needs the same `OTA_SIGNING_KEY` secret and the same `set -o pipefail` discipline as `build-firmware`, and must not echo the key or upload any artifact covering the key path.

**Not affected**
- No firmware behaviour changes — with one user-approved exception (design.md D11): the harness exposed that fresh-device REGISTER never persisted its user (`sdf_app.c` treated only loaded-and-erased slots as free, but a factory-fresh NVS slot returns `ESP_ERR_NVS_NOT_FOUND`), so the slot search now treats a missing slot as free. Every other production source file is exercised as-is; the fixture apps link existing components and call existing public APIs (`sdf_ota_*`, `sdf_ble_companion_open_pairing_window`).

**Risks carried**
- `esp-emu` BLE fidelity is the gating unknown for Layer 2 and is de-risked by a spike before any harness code is written.
- Reimplementing the login challenge response in Python duplicates `sdf_services_web_auth.c`'s scheme; if that scheme changes, the harness breaks. The failure is loud (login rejected), not silent.
