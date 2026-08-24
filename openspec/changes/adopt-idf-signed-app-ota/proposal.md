## Why

The project maintains a hand-rolled OTA image signing scheme — a 68-byte footer (raw ECDSA P-256 `r‖s` plus an `SDF\x01` magic marker), a streaming SHA-256 accumulator, an mbedTLS verification core, a Python signing tool, and a CMake key-generation pipeline — totalling roughly 360 lines of security-critical firmware plus 246 lines of tooling that the team owns and must keep correct. ESP-IDF ships the same guarantee, built on the same primitive (ECDSA over NIST P-256, SHA-256 digest), as a Kconfig option that burns no eFuses: `CONFIG_SECURE_SIGNED_APPS_NO_SECURE_BOOT` is declared `depends on !SECURE_BOOT` and is described by its own help text as protecting against remote network attackers but not physical access — precisely the threat model the custom footer addresses.

Adopting it now, at development stage, costs nothing irreversible and buys a staged upgrade path: the same signed image format is what hardware Secure Boot V2 verifies later, so enabling `CONFIG_SECURE_BOOT` becomes a config change rather than a re-signing migration. The custom scheme's one remaining advantage — verifying before `esp_ota_end()` rather than inside it — saves a flash write, not a security property.

## What Changes

- **BREAKING**: Adopt IDF signed-app verification (`CONFIG_SECURE_SIGNED_APPS_NO_SECURE_BOOT=y`, `CONFIG_SECURE_SIGNED_APPS_ECDSA_V2_SCHEME=y`, `CONFIG_SECURE_BOOT_ECDSA_KEY_LEN_256_BITS=y`, `CONFIG_SECURE_SIGNED_ON_UPDATE_NO_SECURE_BOOT=y`). No eFuse is burned; `CONFIG_SECURE_BOOT` stays `n`.
- **BREAKING**: Delete the custom footer format. Previously signed images (`*_signed.bin` carrying `SDF\x01`) are not accepted by the new firmware, and images signed for the new firmware are not accepted by the old. Signed images gain a 4 KB-aligned pad plus a 4096-byte signature sector instead of a 68-byte footer, so the size a client declares at transfer start changes.
- Remove the streaming SHA-256 accumulator (`sdf_ota_digest_t` and its begin/update/finish/release API) and the ECDSA verification core (`sdf_ota_verify_digest`), which IDF now performs inside `esp_ota_end()` against the staging partition.
- Remove the footer-verification entry points `sdf_ota_verify_footer`, `sdf_ota_verify_signature`, and `sdf_ota_compute_partition_digest`, together with the `sdf_cli` `ota verify` path that is their only remaining caller and its unreliable "assume the image fills the partition" size fallback.
- Remove `CONFIG_SDF_OTA_SIGNATURE_VERIFY` and the fail-open `#else` stub that returns `ESP_OK` when verification is compiled out. Verification becomes non-optional; there is no build configuration in which an unsigned image is accepted.
- **Retain** the transfer-window capture primitive (`sdf_ota_window_capture`) and the `esp_app_desc_t` capture at window `[32, 288)`. The version and downgrade check is project policy independent of signing and must still run without reading the target partition while a handle is open.
- Replace `tools/sdf_sign_ota.py` with IDF's `espsecure.py` / `idf.py` signing, and change key generation to produce the PEM key IDF's signing tooling expects rather than a 65-byte raw EC point embedded via `EMBED_FILES`.
- **BREAKING**: The trust anchor moves from a key blob linked into the app to the signature block of the *currently running app*. With secure boot disabled, `get_secure_boot_key_digests()` (`secure_boot_signatures_app.c:154-158`) takes trusted digests from the running app, so an OTA image is accepted only if signed by the same key as the firmware already on the device. Two consequences: the first image must be flashed **signed** over serial, or every subsequent OTA fails with "Could not read secure boot digests"; and only signature block 0 is consulted (`secure_boot_num_blocks = 1` under `CONFIG_SECURE_SIGNED_ON_UPDATE_NO_SECURE_BOOT`), so key rotation cannot be staged across blocks and requires a serial reflash.
- Accept a host-test coverage reduction: the ECDSA known-answer-vector tests and the streaming-digest chunk-accumulation tests lose their subject, because verification moves into `bootloader_support`, which is not built for `IDF_TARGET=linux`. Replace with an on-target/emulator test that a tampered image is rejected at commit.

## Capabilities

### New Capabilities

None. This change replaces the mechanism behind an existing capability rather than introducing a new one.

### Modified Capabilities

- `ota-signature`: Wholesale replacement of the signing and verification requirements — signature format, where verification runs relative to `esp_ota_end()`, what may be read from the target partition, the public-key embedding, the signing tool, and the removal of the compile-out switch.
- `ota-key-autogen`: The generated key's format and consumer change — from a P-256 private key whose 65-byte uncompressed public point is extracted and embedded via `EMBED_FILES`, to a signing key consumed by IDF's build-time signing.
- `firmware-host-test-runner`: The `sdf_ota` linux-safe coverage requirement currently names `sdf_ota_signature.c`'s ECDSA digest verification core and streaming digest accumulation, both of which are deleted. Coverage narrows to `sdf_ota_version.c` semver comparison and `sdf_ota_window_capture` chunk-boundary behavior, with signature-rejection coverage moving to an automated `esp-emu` run on the `esp32c6` target.
- `firmware-ci`: The workflow gains a signing-key provisioning step sourced from a repository secret, and a third job running the OTA signature verification check under `esp-emu`.

## Impact

**Firmware**
- `firmware/components/sdf_ota/` — `src/sdf_ota_signature.c` (largely deleted; window capture survives), `include/sdf_ota.h` (footer/digest/pubkey constants and verification prototypes removed), `include/sdf_ota_digest.h`, `src/sdf_ota.c` (session struct loses the digest context and footer window; `sdf_ota_verify_and_commit` loses the pre-`esp_ota_end()` verification step and must instead map `esp_ota_end()`'s `ESP_ERR_OTA_VALIDATE_FAILED` to the `OTA_SIGNATURE_INVALID` audit event), `CMakeLists.txt` (key pipeline, `EMBED_FILES`, `sign_ota` target).
- `firmware/components/sdf_cli/sdf_cli_commands.c` — the `ota verify` subcommand.
- `firmware/components/sdf_ota/test/test_sdf_ota_signature.c`, `test_sdf_ota_digest.c`, and their `RUN_TEST` wiring in `firmware/test_runner/main/test_runner_main.c`.
- `firmware/sdkconfig.defaults` — signed-app Kconfig block replaces `CONFIG_SDF_OTA_SIGNATURE_VERIFY`; the entry in `sdf_config/Kconfig` is removed.

**CI**
- `.github/workflows/firmware-ci.yml` — a signing-key step materializing `OTA_SIGNING_KEY` from repository secrets before each firmware build, and a new third job running the signature accept/reject cases under `esp-emu` on `esp32c6`, alongside the existing `build-firmware` and `test-firmware` jobs.
- The same PEM key is used locally and in CI; a repository secret must be created before the workflow change lands, or firmware builds will fail on the missing secret.

**Not affected**
- The BLE transport (`sdf_ble_companion_ota.c`, `sdf_ble_ota_protocol.c`) passes the client-declared `image_size` through without interpreting the footer, so the larger signed-artifact size needs no protocol change.
- No existing workflow under `.github/` or `scripts/` references `sdf_sign_ota.py` or `ota_private.key`, so nothing breaks on their removal.

**Tooling and operations**
- `tools/sdf_sign_ota.py` deleted; the release/signing procedure documented for operators changes.
- `ota_private.key`, `ota_public.key`, `ota_public_key.bin`, `ota_public_key.bin.pem` at the repo root are superseded; `.gitignore` rules must continue to cover the replacement key.
- `web-companion` and any client that uploads firmware must ship the new signed artifact; a device running old firmware cannot be updated to new firmware over BLE without an intermediate flash, since neither accepts the other's format.

**Security posture**
- Unchanged against remote/network attackers. Explicitly *not* protected against an attacker with physical access who can reflash the bootloader — the same as today, since the current scheme's key is embedded in an unsigned, physically rewritable app.
