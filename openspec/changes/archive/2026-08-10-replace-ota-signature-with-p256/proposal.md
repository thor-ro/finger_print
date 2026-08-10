## Why

OTA signature verification does not work and has never worked: `sdf_ota_signature.c`'s verification branch calls `mbedtls_ed25519_init/import_public_key/verify/free`, an API that **does not exist** in the mbedTLS bundled with this project's ESP-IDF (v6.0.2) — no header, no symbols, no PSA driver anywhere in the tree. The branch is gated behind `CONFIG_SDF_OTA_SIGNATURE_VERIFY`, which defaults to `n` (`firmware/sdkconfig.defaults:124`), so it has never been compiled on either target and the breakage stayed invisible while `AGENTS.md:71` advertised "OTA signature verification: Ed25519 (mandatory)". Even if the API existed, the implementation `malloc()`s the entire image for a single-shot verify and caps at 192 KB on a board with ~512 KB SRAM and no PSRAM, while `ota_0`/`ota_1` are ~1.9 MB — every realistic image would fail closed with `ESP_ERR_NOT_SUPPORTED`. For a security-relevant door lock this is the difference between an advertised control and no control at all.

## What Changes

- **BREAKING** Replace Ed25519 with **ECDSA P-256** as the OTA signature algorithm. `CONFIG_MBEDTLS_ECDSA_C=y` and `CONFIG_MBEDTLS_ECP_DP_SECP256R1_ENABLED=y` are already enabled, so this adds no new crypto dependency, versus Ed25519 which would require vendoring and auditing a third-party implementation into firmware.
- **BREAKING** Signature format becomes 64-byte raw `r‖s` (two 32-byte big-endian halves), not ASN.1 DER. Footer stays 68 bytes (`64-byte signature + 4-byte "SDF\x01" magic`), unchanged in size.
- **BREAKING** Embedded public key becomes a 65-byte uncompressed EC point (`0x04 || X || Y`) instead of a 32-byte Ed25519 raw key.
- Sign a **SHA-256 digest of the image** rather than the raw image bytes, making verification memory-bounded: the digest is computed incrementally during `sdf_ota_write()` and the 192 KB in-memory cap and whole-image `malloc()` are removed entirely. Image size stops constraining verification.
- Extract a partition-independent `sdf_ota_verify_digest()` core so real cryptographic verification is testable on the `IDF_TARGET=linux` host runner with NIST CAVP P-256 vectors — replacing today's test, which only pins that the disabled no-op stub returns `ESP_OK`.
- Flip `CONFIG_SDF_OTA_SIGNATURE_VERIFY` to default `y`, so the real path is compiled by every build and CI run.
- Remove the vestigial `#include "sdf_app.h"` from `sdf_ota_signature.c` (no `sdf_app_*` symbol is used), closing the include-without-`REQUIRES` gap flagged as unresolved by the 2026-08-06 linux-target spike.
- Regenerate the signing keypair as P-256 (`prime256v1`) and update `tools/sdf_sign_ota.py` to sign/verify the digest in the new format. No signed images or field-deployed keys exist, so no rotation or compatibility path is required.
- Correct `AGENTS.md:71` to describe the algorithm actually in force.

## Capabilities

### New Capabilities
- `ota-signature`: OTA image signature verification — algorithm, signed-payload definition, streaming digest computation, image format, embedded key, and signing-tool contract. Listed as new because it is **absent from `openspec/specs/`**: the 2026-07-23 `ota-mechanism` change authored an `ota-signature` delta that was never synced to main specs, leaving the requirement unrepresented in the source of truth — a contributing reason the implementation's breakage went unnoticed.

### Modified Capabilities
- `ota-key-autogen`: the auto-generated key is specified as Ed25519; it becomes ECDSA P-256, and the extracted public key becomes 65 uncompressed bytes rather than 32.
- `firmware-host-test-runner`: the "sdf_ota linux-safe subset covered" scenario pins coverage to `sdf_ota_signature.c`'s *default-configuration no-op* path under `CONFIG_SDF_OTA_SIGNATURE_VERIFY=n`; coverage becomes real P-256 digest verification against known-answer vectors.

## Impact

- **Firmware**: `firmware/components/sdf_ota/src/sdf_ota_signature.c` (rewritten verification), `src/sdf_ota.c` (SHA-256 context added to session state; `sdf_ota_begin`/`sdf_ota_write`/`sdf_ota_abort` touched), `include/sdf_ota.h` (new `sdf_ota_verify_digest()`), `CMakeLists.txt` (key generation switches to P-256).
- **Shared write path**: `sdf_ota_write()` is common to all three OTA trigger sources (Zigbee, CLI, BLE companion), so the digest change affects every path. Verified safe under BLE resume: writes are sequential-append only and the client resumes from the device's own confirmed `bytes_written`, so no byte is hashed twice.
- **Tooling**: `tools/sdf_sign_ota.py` (key generation, signing, verification), and any local `ota_private.key` — existing Ed25519 keys become unusable and must be regenerated.
- **Config**: `firmware/sdkconfig.defaults` (`CONFIG_SDF_OTA_SIGNATURE_VERIFY` default flips to `y`).
- **Tests**: `firmware/components/sdf_ota/test/test_sdf_ota_signature.c` replaced with real known-answer vector tests; `firmware/test_runner` wiring.
- **Docs**: `AGENTS.md` Security Defaults.
- **Not affected**: no change to the BLE OTA wire protocol (BEGIN/CHUNK/END framing, `chunk_ack` offsets), the web companion app, version comparison, or rollback behavior.
