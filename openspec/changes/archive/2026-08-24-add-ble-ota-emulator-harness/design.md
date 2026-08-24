## Context

`adopt-idf-signed-app-ota` moves signature verification into `esp_ota_end()` and deletes the host-side crypto tests that used to cover it. Its D8 named an `esp-emu` run as the replacement gate, but scoped it as "drive the BLE OTA protocol against the emulated device" — which bundles two unrelated risks into one deliverable:

```
  What the parent change actually needs to prove
  ──────────────────────────────────────────────
        tampered image  ──▶  esp_ota_end()  ──▶  rejected
                                  ▲
                                  │  this is the security property
                                  │
  What the BLE harness additionally proves
  ────────────────────────────────────────
   central ─pair─▶ login ─▶ BEGIN/CHUNK/END ─▶ sdf_ota_* ─┘
              ▲                    ▲
              │                    └─ transport correctness
              └─ emulator BLE fidelity: UNPROVEN
```

The security property sits behind the transport in the call graph but does not depend on it. Coupling them means an unproven emulator capability gates a check the project needs now.

## Goals / Non-Goals

**Goals**
- Close the D8 gap: an automated, hardware-free check that a tampered or wrong-key image is rejected and a valid one is accepted.
- Cover the companion BLE transport end to end under emulation, if the emulator can carry it.
- Leave all production firmware behaviour, partition layout and advertising policy unchanged.

**Non-Goals**
- Replacing hardware verification. `adopt-idf-signed-app-ota` tasks 6.2–6.7 remain the release check; this is the regression gate.
- Testing pairing, bonding or login as features in their own right. They are means of reaching the OTA characteristic.
- Emulating flash wear, power loss or radio-level failure.

## Decisions

### D1: Layer the change; the signature gate does not go over BLE

Layer 1 drives `sdf_ota_begin/write/verify_and_commit` directly from an on-target fixture. Layer 2 adds the Bumble central over the same three cases.

**Why:** Layer 1 has no unproven dependency and can land and be made a required check immediately. If Layer 2 turns out to be impossible under emulation, the project still has its gate — the fallback costs BLE-transport coverage, not signature coverage. Building them as one deliverable would put the security check behind the risky part.

**Cost:** The three signature cases are asserted twice. That duplication is deliberate: Layer 1 must stand alone if Layer 2 is dropped.

### D2: The fixture app's own signed binary is the valid image

The accept-case image is the fixture application's own signed output, not a separately built artifact.

**Why:** With secure boot disabled the trust anchor is the running app's signature block (`secure_boot_signatures_app.c:154-158`). A separately built accept-case image would need to be signed with the same key as the fixture, and a mismatch would be silent setup breakage that shows up as a confusing accept-case failure. Reusing the fixture's own binary makes the key match structural rather than procedural.

### D3: Store one image, derive the other two on the fly — with integrity repaired

Rather than pre-staging three ~1.18 MB artifacts (~3.5 MB), store the valid image once plus a 4096-byte foreign signature sector:

| Case | How it is produced at run time |
| --- | --- |
| Valid | Written byte-for-byte from the fixture partition |
| Tampered | One byte flipped in a loaded segment, **then the segment checksum byte and the appended SHA-256 recomputed**, keeping the original (now stale) signature sector |
| Foreign key | Same bytes, trailing 4096-byte signature sector substituted |

**Why one image:** Secure Boot V2 puts the signature in a trailing 4096-byte sector, so a foreign-key image differs from the valid one only in that sector. This cuts fixture storage from ~3.5 MB to ~1.19 MB and removes three-way drift between artifacts that must otherwise be regenerated together.

**Why integrity must be repaired.** `esp_image_verify()` runs its checks in a fixed order (`esp_image_format.c:215-232`):

```
process_image_header -> process_segments -> process_checksum
    -> process_appended_hash_and_sig -> verify_secure_boot_signature
                                        ^
                    a naive byte flip never gets here
```

A flipped payload byte fails `process_checksum` — the base image format's simple XOR checksum, which is present regardless of `CONFIG_SECURE_SIGNED_ON_UPDATE`. The run aborts with `ESP_ERR_OTA_VALIDATE_FAILED` before any signature code executes. A tamper case built that way is rejected whether or not signature verification is enabled, so it proves nothing about signatures and would give a false sense of coverage.

Recomputing the checksum and the appended SHA-256 after the flip makes the image internally consistent, so it passes every pre-signature check and can only be rejected at `verify_secure_boot_signature`. This also models the actual threat: an attacker who modifies firmware can trivially repair the checksum, and is stopped only by being unable to forge the signature.

**The discriminating self-test this enables.** With signature verification compiled out, the integrity-repaired tampered image MUST be *accepted*. If it is still rejected, the gate is measuring the checksum rather than the signature and is not a signature gate. This is what task 4.4 asserts, and it is the reason 4.4 exists.

**Constraint:** The flipped byte must land in a loaded segment's data, not in padding or in the signature sector.

### D4: Fixture blob lives in a data partition the production table does not have

The fixture project carries its own partition table adding a `fixtures` data partition at `0x400000`, sized `0x140000`.

**Why:** Flash is 8 MB and the production table ends at `0x400000`, leaving 4 MB unused — so the blob fits without disturbing `ota_0`/`ota_1`/`otadata`, whose offsets the OTA logic depends on. Keeping it in the fixture's own CSV means `firmware/partition_table.csv` is untouched, so nothing about this harness can change what ships.

`idf.py merge-bin` will not populate a partition IDF has no producer for, so the build splices the blob into the merged flash image at the partition offset as an explicit step.

### D5: Run order is reject, reject, accept

**Why:** The accept case commits and repoints the boot partition, so it must run last for the "boot partition unchanged" assertions in the reject cases to mean anything. This order also produces the session-recovery case for free: a rejected transfer followed by a successful one.

### D6: Spike emulator BLE before writing harness code

The first Layer 2 task brings up an unmodified companion build under `esp-emu --ble-hci tcp:` with a minimal Bumble script and establishes only: can a central discover, connect, pair and receive a notification?

**Why:** `esp-emu --help` documents `--elf` as "BLE symbol interception", which suggests it may hook controller symbols rather than emulate the radio. That distinction decides whether pairing and GATT notifications work at all, and it is cheap to answer before any protocol code exists. Writing the wire protocol, login and fixture plumbing first would risk discarding all of it.

**Fallback if the spike fails:** Record the finding, drop Layer 2 to hardware-only coverage under `adopt-idf-signed-app-ota` task 6.6, and keep Layer 1 as the CI gate. The spec requires the outcome be written down either way, so the harness cannot be silently abandoned.

**Outcome: the radio is real; discovery succeeded (task 6.1/6.2).** Booted the unmodified, already-signed production companion build (`firmware/build/sdf.bin`, merged via `idf.py merge-bin`) under `esp-emu --chip esp32c6 --ble-hci tcp:127.0.0.1:<port> --elf firmware/build/sdf.elf`, bridged to a Bumble central (`bumble` 0.0.233, installed via plain `pip install bumble` — no `docker` needed, correcting task 1.3's earlier assumption) through `bumble.controller.Controller` + `bumble.link.LocalLink`, the same architecture `bumble/apps/controllers.py` uses (two `tcp-server:` HCI transports on one shared simulated link; `esp-emu` dials in as the TCP *client* on its side — confirmed via `esp-emu --ble-hci tcp:...` failing with "Connection refused" against an unbound port before the bridge was listening). `device.find_peer_by_name("SDF")` (`sdf_ble_companion.c:1268-1272`) resolved to the device's real address within seconds: `SPIKE_SCAN_RESULT status=SEEN name=SDF address=24:0A:C4:00:00:03/P`. `--elf`'s "BLE symbol interception" wording turned out to mean exactly what it says at the HAL layer (patches `esp_btbb_enable`, `r_ble_hci_trans_hs_cmd_tx`/`_acl_tx`, etc. to redirect the controller-facing side of the NimBLE stack) and nothing about the link itself — the traffic captured was a real, complete, bidirectional HCI session, including a full GAP host-sync sequence, matching what the same firmware does against a real controller.

Two bugs surfaced and were fixed in the spike harness itself (not the firmware) before discovery worked: stock `bumble.controller.Controller` has no handler for `HCI_LE_Set_Privacy_Mode_Command` (answered "Unknown HCI Command", which the NimBLE host tolerated) and defaults `public_address` to `00:00:00:00:00:00`, which NimBLE's `ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, ...)` correctly refuses (`BLE_HS_ENOADDR` — `"Failed to start sparse advertising: 21"` in the UART log). A small `Controller` subclass adding a trivial success handler for that command, plus passing a real (non-all-zero) `public_address`, resolved both; task 8.3's CI wiring must carry this same subclass rather than the bare `bumble.apps.controllers` bridge.

**Connect and pair were not exercised in this pass.** Default advertising is allow-list-filtered with an empty list on a truly unmodified boot (D7), and `sdf_ble_companion_open_pairing_window()` has exactly one call site, gated behind a physical button press (`sdf_app.c:558-569`, `SDF_SERVICES_ADMIN_ACTION_BLE_PAIRING_WINDOW`) with no CLI, Zigbee, or other software-reachable path — confirmed by search, not assumed. Reaching connect/pair/notification therefore requires task 7.1's fixture (a boot-time call to that same public API), which is genuinely group 7's scope, not a spike-harness workaround. The spike's own falsifiable question — real radio vs. cosmetic interception — is answered, and that is a real result: the HCI session is bidirectional, not a stub.

**But D6's gate is only half-passed, and the unanswered half is the one that carries the risk.** This decision's stated rationale was "writing the wire protocol, login and fixture plumbing first would risk discarding all of it" — that risk lives in SMP pairing and GATT notifications, not in discovery. Two independent gaps in Bumble's stock `Controller` had to be patched to reach discovery alone (missing `HCI_LE_Set_Privacy_Mode_Command` handler; all-zero `public_address` rejected by NimBLE as `BLE_HS_ENOADDR`). That is evidence the controller emulation is incomplete relative to what NimBLE exercises, which raises rather than lowers the prior on further gaps in the paths still untested. Treating discovery as sufficient would restore exactly the ordering D6 exists to prevent.

**Outcome: full gate passed (task 6.4) — pairing, notifications and application login all work under emulation; bulk image transfer does not (esp-emu defect).**

With task 7.1's `ble_ota_gate` fixture booted under `esp-emu --ble-hci tcp:`, the harness (`tools/ble_ota_harness/`) completed the whole pre-transfer flow against the real firmware GATT stack: discovery (`SDF` advertisement), connect, LE Secure Connections Just Works pairing with encryption enabled, service/characteristic discovery, CCCD subscription, REGISTER through the admin-action path (the fixture's D10 synthetic match approving it) with the 1-byte AUTH result notification received, challenge-response LOGIN verified by the device (proving the Python PBKDF2/HMAC mirror of D8 byte-for-byte), and the unauthenticated OTA write rejected with `INSUFFICIENT_AUTHENTICATION` (task 7.4's scenario).

Reaching that result required patching four gaps in stock Bumble (all in `tools/ble_ota_harness/bumble_espemu.py`; CI wiring must carry the same subclass):

1. No handler for `HCI_LE_Set_Privacy_Mode` — answered with SUCCESS.
2. All-zero default public address refused by NimBLE's advertising (`BLE_HS_ENOADDR`) — explicit non-zero addresses for both controllers.
3. `bumble.link.LocalLink` stamps LE ACL PDUs with the sender controller's *random* address, which the Device host overwrites at power-on, so every PDU was dropped with "no connection for …" and SMP stalled — routing overridden to use public addresses.
4. Bumble's controller sends Encryption Change without the LTK-request handshake; NimBLE sits in its SMP `LTK_START` state, sees an unexpected Encryption Change and aborts pairing with UNSPECIFIED_REASON — `on_le_encrypted` overridden to emit `HCI_LE_Long_Term_Key_Request` first, plus handlers for its reply/negative-reply.
   Additionally, Bumble's default pairing config requests MITM, which a NoInputNoOutput pair cannot satisfy; the central's pairing config is set to `sc=True, mitm=False, bonding=True` to match the firmware's Just Works flags.

**The blocking defect (recorded per the spec's "Evaluation outcome is recorded"):** sustained inbound ACL transfer wedges inside esp-emu's BLE HCI path. After roughly 28–31 HCI ACL packets delivered to the emulated host since boot — independent of chunk size (60 B and 243 B chunks both stall), pacing (0–50 ms inter-chunk delays), NimBLE buffer pool sizes, light-sleep/tickless configuration, Zigbee radio activity, ATT write type (with/without response), and esp-emu version (0.39.0 *and* 0.40.1) — the emulator stops processing inbound HCI ACL data entirely and silently: no ATT error, no notification, no log line; the BLE OTA idle timer (60 s) eventually fires because the host never saw the next chunk. The device side is provably healthy up to that point (instrumented `sdf_ble_companion_ota.c` logged six successful `sdf_ota_write()` calls), and Bumble's controller logs show the seventh write handed to the emulator's TCP socket. A ~1.18 MB image needs ~4900 inbound packets, so no transfer can complete. Connection recycling + BEGIN-resume (which the wire protocol supports) was also evaluated and blocked: disconnect/reconnect does not recover the wedge within one emulator boot.

**Decision:** Layer 2's pairing/login/access-control coverage is real and kept (tasks 7.2–7.4 verified under emulation); bulk signature-case transfer over BLE (tasks 7.5–7.7, group 8) falls back to hardware coverage per this decision's original fallback and D9 — Layer 1 remains the required CI gate. Revisit if a future esp-emu release fixes the inbound ACL wedge.

### D7: The fixture opens the pairing window; advertising policy is not touched

Default advertising is allow-list filtered (`BLE_HCI_ADV_FILT_CONN`, `sdf_ble_companion.c:1145-1151`), so an unbonded central cannot connect to a device with an empty allow list. The fixture app calls the existing public `sdf_ble_companion_open_pairing_window()` at boot.

**Why:** This is the same path a user takes to pair a new phone, so the harness exercises the real flow. The alternative — relaxing the filter policy for test builds — would mean the tested configuration differs from the shipped one in exactly the area being tested.

### D8: The login challenge response is reimplemented in Python, not extracted

The harness reimplements PBKDF2-HMAC-SHA256 stretching plus the HMAC-SHA256 response (`sdf_services_web_auth.c:36, 47-55`) using `hashlib` and `hmac`.

**Why:** The alternative is exposing the scheme through a host-callable library shared with the firmware, which is a larger change to production code for a test's benefit. The duplication's failure mode is loud — if the scheme changes, login is rejected and the harness fails immediately — rather than silent divergence.

**Constraint:** Parameters (iteration count, salt length, response length, the exact bytes fed to the HMAC) must be read from the firmware, not guessed, and the harness must cite the source it mirrors.

### D9: CI runs Layer 1 as a required check; Layer 2 joins it once proven

The new `ota-signature (esp32c6 — esp-emu)` job runs Layer 1 from the start. Layer 2 is added to the same job after D6's spike succeeds **in full** (task 6.4, not 6.1's discovery-only partial result).

**Why:** The job that `adopt-idf-signed-app-ota` specifies exists as soon as there is something real for it to run, rather than waiting on the whole harness. It needs the same `OTA_SIGNING_KEY` secret, the same `set -o pipefail` discipline, and must not echo the key or upload artifacts covering it.

### D10: The fixture substitutes a synthetic ADMIN match for the absent fingerprint sensor

Task 7.3's REGISTER over BLE does not only need the pairing window (D7): registering a user routes through `sdf_services_request_admin_action(SDF_SERVICES_ADMIN_ACTION_WEB_REG_AUTH)` (`sdf_ble_companion.c`'s REGISTER handler), which is normally resolved by a real fingerprint sensor producing an ADMIN-permission match via `sdf_services_try_claim_admin_action()` on the `sdf_match_task` path. An emulator has no sensor, so a fixture that only opened the pairing window would stall at REGISTER forever.

The `ble_ota_gate` fixture therefore runs a background task that repeatedly offers a synthetic ADMIN-permission `sdf_fingerprint_match_t` to the same public-internal function a real admin's finger press satisfies, on the same ~poll cadence.

**Why this shape:** It reuses the exact claim path production uses (no new authorization surface is added anywhere), it is a harmless no-op whenever no admin action is pending, and it lives entirely inside the fixture app — no production source is touched. The alternative of stubbing out the admin-action requirement in a component would mean the harness exercises a weaker authorization flow than ships, defeating the point of testing over the real transport.

**Scope guard:** This task exists only in `firmware/ble_ota_gate/main/ble_ota_gate_main.c`. Production's pairing window stays reachable solely through the physical-button + Admin-fingerprint handler (`sdf_app.c:558-569`).

### D11: Fix fresh-device REGISTER persistence in production (approved deviation from "no production changes")

The harness's live REGISTER run exposed a real production bug: `sdf_app_on_web_reg_auth_result()` (`sdf_app.c:995-997`) only saved an authorized user into a slot where `sdf_storage_web_user_load() == ESP_OK && !existing.valid`, but on a factory-fresh device the NVS namespace/key does not exist yet, so every load returns `ESP_ERR_NVS_NOT_FOUND`, the loop never saves, and the very first REGISTER can never persist its user — every subsequent LOGIN_VERIFY is necessarily checked against the pseudo-salt challenge and fails. Without this fix, task 7.3's scenario ("a fixture with no provisioned user") cannot pass on hardware either.

**Decision (user-approved):** fix the production slot-search to treat a missing slot (`ESP_ERR_NVS_NOT_FOUND`) as free, in `sdf_app.c`. This is the one intentional production source change in this otherwise fixture-only change; it is recorded here, in the proposal's Impact section, and in task 9.3's amended wording. The alternative — pre-seeding empty slots from the fixture only — would have left a real first-pairing bug in production while making the test pass.

## Risks / Trade-offs

- **Emulator BLE may not carry pairing or GATT.** Highest-likelihood risk. Mitigated by D6's spike-first ordering and D1's layering; the documented fallback preserves the security gate.
- **Emulator timing differs from hardware.** A transfer that passes under emulation could still fail on a real link. Mitigated by keeping hardware tasks 6.2–6.7 as the release check — this gate catches regressions, it does not certify releases.
- **Fixture drift.** If the signed-image format changes, the stored blob and the foreign signature sector go stale. Mitigated by D2 — the valid image is the fixture's own build output, regenerated every run — and by the foreign sector being the only static artifact.
- **Duplicated assertions between layers.** Accepted per D1.
- **Login scheme duplication.** Accepted per D8; failure is loud.

## Migration Plan

1. Land `adopt-idf-signed-app-ota` first — this change verifies its mechanism.
2. Build Layer 1 and wire it as the CI job. At this point the D8 gap is closed and `adopt-idf-signed-app-ota` tasks 7.1–7.4 and 8.4 can be annotated as superseded.
3. Run the D6 spike: discovery (6.1, done), then task 7.1's pairing-window fixture, then connect/pair/notify (6.4). On full success, build Layer 2 (7.2-7.7, 8) and add it to the job. On failure at 6.4, record the outcome and close Layer 2 as not viable under emulation.
4. Hardware tasks 6.2–6.7 in the parent change proceed independently and remain the release check.
