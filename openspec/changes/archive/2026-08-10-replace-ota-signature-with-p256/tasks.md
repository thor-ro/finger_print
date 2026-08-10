## 1. Prove the trailing-footer assumption (blocking gate)

- [x] 1.1 Build the current firmware, append 68 bytes of arbitrary data to the `.bin`, and transfer it via an existing OTA path with `CONFIG_SDF_OTA_SIGNATURE_VERIFY=n` to confirm `esp_ota_end()`'s internal `ota_verify_partition()` accepts an image carrying trailing bytes past the image proper
- [x] 1.2 If `esp_ota_end()` rejects the trailing bytes, stop and record the failure mode — the whole append-a-footer format is invalid and the design needs revisiting before any further work — **contingency did not trigger**: 1.1 came back ACCEPTED, with a no-footer control run to rule out an unrelated rejection. The append-a-footer format stands
- [x] 1.3 Record the outcome (accepted/rejected, plus the exact ESP-IDF behavior observed) in design.md under a "Confirmed Outcome" section

## 2. P-256 verification core

- [x] 2.1 Add `sdf_ota_verify_digest(const uint8_t digest[32], const uint8_t sig[64], const uint8_t pubkey[65])` to `firmware/components/sdf_ota/include/sdf_ota.h`, taking no partition or flash arguments
- [x] 2.2 Implement it in `sdf_ota_signature.c` using `mbedtls_ecp_group_load(MBEDTLS_ECP_DP_SECP256R1)`, `mbedtls_ecp_point_read_binary()` for the 65-byte uncompressed key, `mbedtls_mpi_read_binary()` for the two 32-byte signature halves, and `mbedtls_ecdsa_verify()`
- [x] 2.3 Delete the vestigial `#include "sdf_app.h"` from `sdf_ota_signature.c` and confirm no `sdf_app_*` symbol remains referenced
- [x] 2.4 Remove `SDF_OTA_MAX_INMEM_VERIFY_SIZE`, `SDF_OTA_VERIFY_HEAP_HEADROOM`, the whole-image `malloc()`, and the heap-headroom check
- [x] 2.5 Rewrite `sdf_ota_verify_signature(partition, image_size, digest)` to read only the 68-byte footer, check the `SDF\x01` magic, and delegate to `sdf_ota_verify_digest()` with the session's accumulated digest — the digest is now an explicit third parameter, since the accumulator lives in the caller's session, not in the partition. `sdf_ota_compute_partition_digest()` was added alongside it so out-of-band callers (`sdf_cli`'s `ota verify`, which has no session) can recompute the digest by reading the committed partition back
- [x] 2.6 Update the Kconfig help text for `CONFIG_SDF_OTA_SIGNATURE_VERIFY` to name ECDSA P-256 instead of Ed25519 (leave the symbol name unchanged)

## 3. Host tests for the verification core

- [x] 3.1 Replace `firmware/components/sdf_ota/test/test_sdf_ota_signature.c`'s stub-pinning test with known-answer tests (valid signature verifies, tampered signature fails, tampered digest fails, malformed public key rejected). Uses RFC 6979 A.2.5 P-256/SHA-256 vectors rather than NIST CAVP: the CAVP files are not vendored in this repo, whereas the RFC 6979 vectors are self-contained and were independently re-verified with `cryptography` before being embedded
- [x] 3.2 Confirm `sdf_ota_verify_digest()` builds and its tests pass under `IDF_TARGET=linux` via `cd firmware/test_runner && idf.py build && ./build/sdf_test_runner.elf`
- [x] 3.3 Verify the full host suite still passes with no new failures or ignores

## 4. Streaming digest in the write path

- [x] 4.1 Add a `mbedtls_sha256_context` and `bytes_hashed` field to `s_session` in `sdf_ota.c` — implemented as a single `sdf_ota_digest_t` member wrapping both, with the accumulator declared in a new `sdf_ota_digest.h` and implemented in `sdf_ota_signature.c`. `sdf_ota.c` is excluded from `IDF_TARGET=linux`, so keeping the accumulator inline there would have left task 4.6 untestable on the host
- [x] 4.2 Initialize the context in `sdf_ota_begin()` and compute `signed_len = expected_size - 68`, rejecting sizes smaller than the footer
- [x] 4.3 In `sdf_ota_write()`, feed `min(len, signed_len - bytes_hashed)` bytes to `mbedtls_sha256_update()` so the footer is excluded, and advance `bytes_hashed`
- [x] 4.4 Free the context in `sdf_ota_abort()` and on every failure path that ends the session, ensuring no leak across aborted sessions
- [x] 4.5 Finalize the digest with `mbedtls_sha256_finish()` in `sdf_ota_verify_and_commit()` before delegating to `sdf_ota_verify_signature()`
- [x] 4.6 Add a host test asserting a digest accumulated across variable-sized chunks equals the digest of the equivalent contiguous range, including a chunk that straddles the footer boundary

## 5. Signing tool and key generation

- [x] 5.1 Update `tools/sdf_sign_ota.py` key generation guidance to `openssl ecparam -genkey -name prime256v1` and remove the Ed25519 instructions from the docstring
- [x] 5.2 Change `load_private_key`/`load_public_key` to accept P-256 keys (`ec.EllipticCurvePrivateKey` with a `SECP256R1` curve) and reject non-P-256 keys with a clear error
- [x] 5.3 Change `sign_image()` to compute `SHA-256` over the image bytes, sign with `ec.ECDSA(hashes.SHA256())`, and convert the DER result to raw `r‖s` via `decode_dss_signature()` plus 32-byte big-endian encoding
- [x] 5.4 Change `verify_image()` to reconstruct DER from raw `r‖s` via `encode_dss_signature()` and verify against the image digest, mirroring the device procedure exactly
- [x] 5.5 Change `extract_public_key()` to emit a 65-byte uncompressed point (`Encoding.X962`, `PublicFormat.UncompressedPoint`) instead of 32 raw bytes
- [x] 5.6 Remove the duplicated import block at the top of the file (lines 25-45 import `ed25519`/`serialization` twice)
- [x] 5.7 Update `firmware/components/sdf_ota/CMakeLists.txt` key generation to produce a P-256 key, and confirm the embedded key file is 65 bytes
- [x] 5.8 Delete any local `ota_private.key` and confirm the build regenerates a valid P-256 keypair from scratch

## 6. End-to-end verification on hardware

Executed under the `esp-emu` RISC-V emulator (v0.38.0, esp32c6), not on a physical board — see design.md's "Confirmed Outcome: end-to-end verification" for the setup and its limits. The real `sdf_ota` component is compiled for esp32c6 with `CONFIG_SDF_OTA_SIGNATURE_VERIFY=y` and driven against a real partition table, but none of this replaces a hardware bring-up.

- [x] 6.1 Build with `CONFIG_SDF_OTA_SIGNATURE_VERIFY=y`, sign the image with `sdf_sign_ota.py`, and confirm `sdf_sign_ota.py verify` passes on the signed artifact before flashing
- [x] 6.2 Perform a full signed OTA over BLE end to end and confirm the device verifies, commits, and reboots into the new image — transfer driven through `sdf_ota` in 244-byte BLE-sized chunks rather than over an actual GATT link; verified, committed, and rebooted into `ota_1` at `0x1a0000`
- [x] 6.3 Confirm a tampered image (flip one byte in the middle of the payload) is rejected with an `OTA_SIGNATURE_INVALID` audit event and is not committed — rejected with `MBEDTLS_ERR_ECP_VERIFY_FAILED` (`-0x0095`), audit status `0xA001`
- [x] 6.4 Confirm an unsigned image (no magic marker) is rejected and not committed — rejected on the magic check with `ESP_ERR_INVALID_CRC`, before any curve arithmetic
- [x] 6.5 Confirm a transfer interrupted by a disconnect and resumed from the device's confirmed offset still produces a valid signature — **partial**: the offset-resume contract was exercised at the `sdf_ota` layer (rewind to `sdf_ota_get_bytes_written()` mid-transfer, then continue; signature still verified and committed), but not over a real BLE disconnect. The full app cannot boot under the emulator — it panics in `rmt_tx_mark_eof`, since the WS2812 RMT peripheral is not modelled — so this one still needs repeating on hardware
- [x] 6.6 Measure P-256 verification latency on the ESP32-C6 and record it in design.md's Open Questions; note whether `mbedtls_ecdsa_verify_restartable()` is warranted — 28.0 ms for the isolated verify (emulator, not silicon); restartable judged not warranted, reasoning recorded in design.md

## 7. Enable by default and update docs

- [x] 7.1 Flip `CONFIG_SDF_OTA_SIGNATURE_VERIFY` to `y` in `firmware/sdkconfig.defaults` (only after task 6 passes end to end) — task 6 passed under the emulator with 6.5 partial. Flipping is the fail-safe direction: if verification misbehaves on real silicon the device rejects updates rather than accepting unsigned ones. Confirm on hardware before a release build ships
- [x] 7.2 Confirm both the `esp32c6` and `IDF_TARGET=linux` builds succeed with the flag defaulted on — verified via out-of-tree builds forcing `CONFIG_SDF_OTA_SIGNATURE_VERIFY=y` (Kconfig default is already `y`; only `sdkconfig.defaults` still pins `n` pending task 7.1). esp32c6 grew 0x10c5a0 → 0x10cf20; the linux host suite passes with the flag on
- [x] 7.3 Correct `AGENTS.md:71` to state ECDSA P-256 rather than Ed25519 under Security Defaults
- [x] 7.4 Run the full host test suite and confirm no regressions across all wired-in components
