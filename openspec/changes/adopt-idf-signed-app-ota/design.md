## Context

See `proposal.md` — Why. This section records only the mechanics that shape the approach, all verified against ESP-IDF v6.0.2 at `~/.espressif/v6.0.2/esp-idf`.

The chain that replaces the custom footer:

```
sdf_ota_verify_and_commit()
  └─ esp_ota_end(handle)                               esp_ota_ops.c:565
       ├─ flush partial_bytes to staging                          :583
       ├─ ota_verify_partition()                                  :596
       │    └─ esp_image_verify(ESP_IMAGE_VERIFY, staging)        :527-536
       │         └─ SECURE_BOOT_CHECK_SIGNATURE == 1              esp_image_format.c:40-45
       │              (app build + CONFIG_SECURE_SIGNED_ON_UPDATE)
       │              └─ verify_secure_boot_signature()                      :232
       │                   ├─ SHA-256 over image padded to 4 KB
       │                   ├─ read sig block at src_addr + padded_length
       │                   └─ esp_secure_boot_verify_sbv2_signature_block()
       │                        └─ get_secure_boot_key_digests()
       │                             secure_boot_signatures_app.c:154-158
       │                             !esp_secure_boot_enabled()
       │                               → digests from RUNNING APP
       └─ staging → final copy, only if verification passed       :592-595
```

Three facts from this chain drive every decision below:

1. **`CONFIG_SECURE_SIGNED_ON_UPDATE` is auto-derived.** `Kconfig.projbuild:456-459` sets it `default y` with `depends on SECURE_BOOT || SECURE_SIGNED_ON_UPDATE_NO_SECURE_BOOT`. In an app build that flips `SECURE_BOOT_CHECK_SIGNATURE` to 1, which is what puts signature verification inside `esp_ota_end()`. No project code calls into it.
2. **The trust anchor is the running app.** With `CONFIG_SECURE_BOOT` off, `get_secure_boot_key_digests()` calls `esp_secure_boot_get_signature_blocks_for_running_app(true, ...)`. There is no eFuse, and no key linked into the binary. Trust chains from whatever is currently installed.
3. **Only block 0 is consulted.** `secure_boot_signatures_app.c:234-238` sets `secure_boot_num_blocks = 1` under `CONFIG_SECURE_SIGNED_ON_UPDATE_NO_SECURE_BOOT` (versus `SECURE_BOOT_NUM_BLOCKS` = 3 with hardware secure boot). Multi-key rotation is unavailable in this configuration.

What survives from the current implementation: `sdf_ota_window_capture()` and the `esp_app_desc_t` capture at `[32, 288)`. The version/downgrade check is project policy with no ESP-IDF equivalent, and it still must not read the target partition while a handle is open. Its host tests survive with it.

## Goals / Non-Goals

**Goals:**
- Signature verification performed entirely by ESP-IDF, with zero project-owned cryptographic code.
- No eFuse written by any build, flash, or update operation.
- Signed images that are format-identical to what hardware Secure Boot V2 would verify, so enabling `CONFIG_SECURE_BOOT` later is a config change and not a re-signing migration.
- The commit path keeps its current failure semantics: a rejected image releases the session cleanly and emits `OTA_SIGNATURE_INVALID`.

**Non-Goals:**
- Enabling hardware secure boot, flash encryption, or any eFuse-backed feature. Deliberately deferred; this change is meant to make that a later config flip.
- Signing the bootloader. Without secure boot the bootloader is physically rewritable regardless, so signing it buys nothing.
- Over-the-air key rotation. Ruled out by fact 3 above, not by scope preference.
- Changing the BLE OTA wire protocol. `sdf_ble_ota_protocol.c` passes the client-declared `image_size` through untouched; a larger signed artifact needs no protocol change.

## Decisions

### D1: `SECURE_SIGNED_APPS_NO_SECURE_BOOT` over hardware Secure Boot V2

`Kconfig.projbuild:516-524` declares this option `depends on !SECURE_BOOT`, with help text scoping it to "secured against remote network access, but not physical access."

That is the current security posture exactly. Today's key is embedded in an unsigned, physically rewritable app, so an attacker with physical access already wins. This change is posture-neutral against remote attackers and honest about physical ones.

*Alternative — enable `CONFIG_SECURE_BOOT` now:* rejected. eFuse burning is irreversible per device and the user has scoped this to development stage. The signed image format is identical either way, so nothing is foreclosed.

*Alternative — keep the custom footer alongside IDF verification:* rejected. Belt-and-braces here means maintaining the 360 lines this change exists to delete, for a property IDF already provides. The one asymmetry — the custom path rejects before the flash write completes — saves a write, not a compromise.

### D2: Delete rather than deprecate

`SDF_OTA_SIG_SIZE`, `SDF_OTA_MAGIC_SIZE`, `SDF_OTA_FOOTER_SIZE`, `SDF_OTA_DIGEST_SIZE`, and `SDF_OTA_PUBKEY_SIZE` appear nowhere outside `sdf_ota/` and its tests — verified by grep across `firmware/`. The blast radius is one component plus the `sdf_cli` `ota verify` subcommand.

There is no compatibility window worth preserving: the two formats are mutually unreadable, so a device must be reflashed over serial to cross the boundary regardless of how long the old code lingers. Keeping it only preserves the fail-open stub.

### D3: `SDF_OTA_MIN_IMAGE_SIZE` keeps the descriptor floor, drops the footer term

Currently `32 + 256 + 68`. The `esp_app_desc_t` capture window `[32, 288)` is unchanged, so the floor becomes `SDF_OTA_APP_DESC_OFFSET + SDF_OTA_APP_DESC_SIZE` = 288. `sdf_ota_begin()` must not attempt to derive a signed length; there is no project-defined signed range any more.

Layering the signature sector into the minimum is tempting but wrong: the sector's position depends on 4 KB padding of the actual image, and ESP-IDF validates its presence itself. A project-side check would duplicate that and drift from it.

### D4: Map `ESP_ERR_OTA_VALIDATE_FAILED` to `SDF_ERR_OTA_SIGNATURE_INVALID`

`esp_ota_end()` returns `ESP_ERR_OTA_VALIDATE_FAILED` for any `esp_image_verify()` failure — signature, checksum, or malformed header alike. `sdf_ota_verify_and_commit()` maps it to the existing `SDF_ERR_OTA_SIGNATURE_INVALID` and emits `OTA_SIGNATURE_INVALID`.

The audit event is therefore slightly broader than its name: a corrupt-but-unsigned-correctly image also reports as a signature failure. Accepted — ESP-IDF does not distinguish the causes in its return value, and the operational response (reject, do not commit, re-send) is the same for all of them. The error code is retained rather than renamed so audit-log consumers keep working.

### D5: Key generation stays in CMake, but produces PEM and stops extracting

The existing `sdf_ota/CMakeLists.txt` block already generates a P-256 key via `openssl ecparam -genkey -name prime256v1` — the same curve and format `CONFIG_SECURE_BOOT_SIGNING_KEY` wants. The generation step survives nearly unchanged; what goes is the `extract-pubkey` step, the 65-byte size assertion, the `EMBED_FILES` entry, and both custom targets.

The key path moves to a project-root PEM that `CONFIG_SECURE_BOOT_SIGNING_KEY` names, and signing becomes part of `idf.py build` via `CONFIG_SECURE_BOOT_BUILD_SIGNED_BINARIES` rather than a separate `idf.py sign_ota` step. This is a workflow improvement worth calling out: an unsigned artifact can no longer be produced by forgetting a step.

The spec requires the build to surface when it generated a key rather than reused one, because with the running-app trust anchor a silently regenerated key produces images the fleet rejects — a failure that would otherwise only appear at OTA time.

### D6: Drop `sdf_cli`'s `ota verify` rather than port it

Its size fallback already warns that assuming the image fills the partition "is rarely true," and `esp_secure_boot_verify_signature(src_addr, length)` needs the same length the command cannot reliably obtain. Porting it would carry the unreliable heuristic forward.

The behavior it provided — did this image verify — is now answered by whether the commit succeeded, which the BLE transport already reports. `ota status` and `ota version` are unaffected.

### D7: One signing key, held locally and as a GitHub repository secret

The same P-256 PEM key serves both environments. Locally it lives at the project root as `ota_signing_key.pem`, gitignored, auto-generated on first build. In CI it is injected from a repository secret (`OTA_SIGNING_KEY`, the PEM's contents) and written to that same path before `idf.py build` runs.

Using one key rather than separate dev and CI keys is deliberate, and follows from the running-app trust anchor: a device flashed with a locally built image can only accept OTA images signed by the same key, so a distinct CI key would mean CI-built firmware could never update a locally provisioned device. That is exactly the class of failure this scheme makes irreversible without a serial reflash.

The CMake auto-generation fallback stays in place for CI, but is a failure signal there, not a convenience: if the secret is missing, the build mints a throwaway key and produces firmware that no provisioned device will accept. The build message from task 2.2 is what makes that visible, and CI checks for it explicitly rather than trusting the build to fail on its own.

*Alternative — separate CI signing key:* rejected for the reason above. *Alternative — commit an encrypted key:* rejected; GitHub secrets already solve this without adding a decryption step to every local build.

### D8: The on-target signature test runs under `esp-emu` in CI

`AGENTS.md` already documents the chip-target emulator pattern — an out-of-tree `esp32c6` build, `merge-bin`, then `esp-emu --chip esp32c6 --firmware ... --elf ... --exit-on`. The signature test reuses it rather than inventing a second harness, and runs as a third CI job alongside the existing build and host-test jobs.

Both fixtures are produced by the same CI build with the same key, which is what makes the accept case meaningful: the emulated device's running app and the OTA image it receives are signed by one key, matching the production trust relationship. The reject fixtures are derived by flipping a byte in the signed image's payload, and by re-signing with a throwaway key generated in the job.

The emulator is the gate, not a smoke test. Per this project's prior experience, an `esp-emu` panic has repeatedly turned out to reproduce on hardware, so a failure here fails the check rather than being retried or downgraded. Hardware confirmation before archiving (task 6.7) stays, but is a release step rather than the CI gate.

*Alternative — leave it a manual hardware check:* rejected. The host tests this change deletes were the only automated coverage of the verification path; replacing them with a manual step is a net loss in exactly the area where the previous implementation rotted unnoticed.

## Risks / Trade-offs

**Bricking the OTA path by flashing unsigned firmware** → The highest-consequence risk in this change. If any device is flashed with an unsigned image, `get_secure_boot_key_digests()` fails and every subsequent OTA is rejected with "Could not read secure boot digests" — recoverable only over serial. Mitigation: `CONFIG_SECURE_BOOT_BUILD_SIGNED_BINARIES` makes the normal build output signed, so the unsigned artifact is not the one on disk; the provisioning requirement is captured as a spec requirement rather than documentation; the on-target test asserts a correctly signed image is accepted, which fails loudly if signing regressed.

**Silent key regeneration bricks the fleet's OTA path** → Same failure, different trigger: deleting the PEM makes CMake mint a new key, and every field device then rejects every image. Mitigation: `.gitignore` coverage plus a build-time message distinguishing "generated" from "reused"; CI injects the key from secrets.

**Host-test coverage loss** → The ECDSA known-answer vectors and digest chunk-accumulation tests lose their subject. This is a genuine reduction, and it is worth naming plainly that the previous implementation's defect was caught precisely because a host test could reach the crypto. Mitigation is partial: what is lost is coverage of *ESP-IDF's* crypto, which is not project-maintained, whereas what is gained is an end-to-end on-target assertion that a tampered image is rejected — closer to the property that actually matters. Window-capture and semver coverage are unaffected.

**Larger signed artifact** → 4 KB padding plus a 4096-byte signature sector replaces 68 bytes, so a signed image grows by up to ~8 KB. Over BLE at MTU-sized chunks that is a measurable transfer-time increase and it consumes OTA partition headroom. Mitigation: verify the padded artifact still fits the OTA partition as an explicit task; the transport itself needs no change.

**Verification moves after the flash write** → A bad image is fully written to staging before being rejected, where today it is rejected before `esp_ota_end()`. Mitigation: none needed for correctness — `esp_ota_ops.c:592-595` gates the staging-to-final copy on verification, and the boot partition switch is separate. The cost is one wasted write cycle per rejected image.

**Emulator fidelity for the signature path** → `esp-emu` may not model signature verification identically to hardware. Mitigation: treat an emulator failure as real (consistent with prior experience on this project) and confirm the accept and reject cases on hardware before archiving. The residual exposure is the inverse case — the emulator passing something hardware would reject — which task 6.7's hardware run covers before archive.

**A missing CI secret silently produces unusable firmware** → Without `OTA_SIGNING_KEY`, CMake generates a throwaway key and the build succeeds, yielding artifacts no provisioned device will accept. Mitigation: the CI job asserts the key was materialized from the secret and fails the check on the "generated a new signing key" build message (D7).

**One key across local and CI** → A single key means a single compromise radius, and no environment separation. Accepted: the running-app trust anchor makes cross-key updates impossible, so separate keys would break the ability to update locally provisioned devices from CI-built firmware. Rotation in either case is a serial reflash.

## Migration Plan

1. Land the config, build, and firmware changes together; there is no intermediate state where both schemes work.
2. Build, confirm the artifact is signed, and confirm the padded image plus signature sector fits the OTA partition.
3. Flash the signed image over serial to every development device. This is mandatory — it is what establishes the trust anchor.
4. Verify OTA end-to-end: a correctly signed image commits; a tampered one is rejected with `OTA_SIGNATURE_INVALID` and leaves the device bootable on the old image.
5. Update operator-facing docs (`version.md`, `README.md`, `AGENTS.md` as applicable) to replace `idf.py sign_ota` with the ESP-IDF procedure, and record the serial-flash provisioning requirement.
6. Remove the superseded root key artifacts from the working tree and confirm `.gitignore` covers the replacement.

**Rollback**: revert the commit and reflash over serial. There is no over-the-air rollback across this boundary in either direction, because neither firmware accepts the other's image format. This is why step 3 is not optional and why the change should land before any field deployment.

