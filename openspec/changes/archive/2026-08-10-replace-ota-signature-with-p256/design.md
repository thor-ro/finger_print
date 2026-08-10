## Context

`sdf_ota_signature.c`'s `#if CONFIG_SDF_OTA_SIGNATURE_VERIFY` branch has four independent, stacked defects, each of which alone prevents any signature from ever being verified:

1. **Policy** — `CONFIG_SDF_OTA_SIGNATURE_VERIFY=n` (`firmware/sdkconfig.defaults:124`) selects the `#else` stub, which is `return ESP_OK;` unconditionally. `AGENTS.md:71` claims "Ed25519 (mandatory)"; that line is false today.
2. **Build graph** — the branch `#include "sdf_app.h"` while `sdf_ota/CMakeLists.txt` never lists `sdf_app` in `REQUIRES`/`PRIV_REQUIRES`. Flagged as an unresolved Open Question by the 2026-08-06 linux-target spike.
3. **API surface** — the branch calls `mbedtls_ed25519_init/import_public_key/verify/free` from `mbedtls/ed25519.h`. **None of this exists.** Searching the ESP-IDF v6.0.2 mbedTLS tree (`~/.espressif/v6.0.2/esp-idf/components/mbedtls`) for `ed25519`/`edwards25519` returns zero hits in the PSA core and drivers, and `PSA_WANT_ECC_TWISTED_EDWARDS_*`/`PSA_WANT_ALG_PURE_EDDSA` appear nowhere. `PSA_ALG_PURE_EDDSA` and `PSA_ALG_ED25519PH` exist as constants in `psa/crypto_values.h` for spec completeness, but no driver implements them — using them would compile and fail at runtime with `PSA_ERROR_NOT_SUPPORTED`. This code has never linked.
4. **Memory model** — `mbedtls_ed25519_verify()` (real or not) takes the whole message in one buffer, so the code `malloc()`s the entire image, capped at `SDF_OTA_MAX_INMEM_VERIFY_SIZE` = 192 KB on a board with ~512 KB SRAM and no PSRAM. `ota_0`/`ota_1` are ~1.9 MB each, so every realistic image falls through to `ESP_ERR_NOT_SUPPORTED` and fails closed.

The 2026-08-06 spike judged this branch low-risk partly on the basis that Ed25519 was "already linux-tested elsewhere in this repo, per the Nuki crypto suite." That was a mix-up: `sdf_nuki_crypto.c` implements Curve25519 (Montgomery form) for X25519/ECDH plus salsa20/poly1305 for the Nuki `crypto_box` scheme. Same underlying field, different curve form, not reusable for Ed25519 signature verification. That false premise is why the fictional API survived review.

Constraints: ESP32-C6, ~512 KB SRAM, no PSRAM. No signed images and no field-deployed keys exist, so there is no compatibility or key-rotation burden. The device is security-relevant (a door lock), so the target posture is mandatory verification backed by a proven, already-linked implementation rather than a novel dependency.

## Goals / Non-Goals

**Goals:**
- A signature path that actually compiles, links, and verifies, on both `esp32c6` and `IDF_TARGET=linux`.
- Verification memory cost independent of image size — no cap, no whole-image buffer.
- Real cryptographic assertions in the host test suite (known-answer vectors), not stub-pinning.
- Default-on, so the path cannot silently rot again.

**Non-Goals:**
- ESP-IDF Secure Boot v2 / bootloader-side verification. Different trust model, already rejected by the 2026-07-23 design; app-side verification before commit is retained.
- Encrypted OTA payload — the signature provides authenticity; BLE transport is already encrypted.
- Changing the BLE OTA wire protocol (BEGIN/CHUNK/END framing, `chunk_ack` offsets), the web companion, version comparison, or rollback behavior.
- Anti-rollback / minimum-version enforcement — unchanged from the existing design.
- Making `sdf_ota.c`'s partition-write/rollback logic host-testable (still blocked by its `sdf_app_emit_audit` link dependency); only the verification core becomes host-testable here.

## Decisions

### D1. ECDSA P-256 over Ed25519

`CONFIG_MBEDTLS_ECDSA_C=y` and `CONFIG_MBEDTLS_ECP_DP_SECP256R1_ENABLED=y` are already set in `firmware/sdkconfig`, and `mbedtls_ecdsa_verify()` / `mbedtls_ecp_point_read_binary()` are real, present, and already linked. ECDSA is also inherently digest-based — `mbedtls_ecdsa_read_signature(ctx, hash, hlen, sig, slen)` takes a hash, never a message — so it matches the streaming design by construction rather than by workaround.

*Alternatives considered:*
- **Keep Ed25519, vendor an implementation** (ref10-style library) — rejected. Adds new unaudited supply-chain surface to a security-relevant device. "Mandatory signature backed by a proven, already-linked library" is a stronger posture than "mandatory signature backed by a novel dependency."
- **PSA `PSA_ALG_ED25519PH`** — rejected. The constants exist but no driver implements them; this is the exact "looks real in the header, dead in practice" trap that produced defect 3.
- **Secure Boot v2** — out of scope (see Non-Goals).

### D2. 64-byte raw `r‖s`, not ASN.1 DER

Two 32-byte big-endian halves; footer stays 68 bytes (`64 + 4` magic), identical in size to today's.

This is not cosmetic — it interlocks with D3. DER is length-variable (70–72 bytes depending on leading-zero rules), but streaming requires `signed_len = expected_size − FOOTER_SIZE` to be computable **at BEGIN, before any bytes arrive**. A variable-length footer makes that impossible without padding or a length prefix. Raw fixed 64 keeps `FOOTER_SIZE` a compile-time constant.

Firmware loads the halves with `mbedtls_mpi_read_binary()` and calls `mbedtls_ecdsa_verify(grp, digest, 32, Q, r, s)` directly, rather than the `read_signature()` convenience wrapper that expects DER. The tool uses `cryptography`'s `decode_dss_signature()` to split its DER output into `r`/`s`.

### D3. Compute SHA-256 during write, not by reading the partition back at commit

The digest is accumulated in `mbedtls_sha256_update()` calls inside `sdf_ota_write()`, with the context living in `s_session`. The alternative — looping `esp_partition_read()` over the written image at commit time — is a smaller, better-contained diff (it would stay entirely inside `sdf_ota_signature.c`) and was the initial preference, but two findings rule it out:

- **`esp_ota_write()` defers the trailing ≤15 bytes.** `esp_ota_ops.c:372-376` stashes `size % 16` in `partial_data` and only flushes it in `esp_ota_end()` (line 583). It is gated on `esp_efuse_is_flash_encryption_enabled()`, so read-back is correct *today* — flash encryption is currently off. But this is a door lock with `CONFIG_SECURE_BOOT_V2_PREFERRED=y` and full flash-encryption SOC support already in config; enabling it is a *when*, not an *if*. The failure mode is vicious: the last ≤15 bytes read back as `0xFF`, the digest mismatches, the signature fails, and OTA is permanently blocked with a crypto error pointing nowhere near the cause.
- **Hashing during write is safe under resume.** `sdf_ota_write()` (`sdf_ota.c:200-208`) is a pure sequential append — `esp_ota_write()` then `bytes_written += len`, no offset addressing — and the BLE resume protocol has the client resume from the device's own confirmed `bytes_written` (`sdf_ble_companion_ota.c:131-137`). The device never rewinds and never rewrites a byte, so every byte is hashed exactly once, in order.

Hashing during write is additionally immune to any future change in `esp_ota_write()` buffering semantics, and saves a full read pass over a ~1.9 MB partition per update.

**Footer exclusion:** the client streams the whole signed file *including* the 68-byte footer, but the signature covers only `expected_size − 68` bytes. `sdf_ota_write()` therefore feeds the hash `min(len, signed_len − bytes_hashed)` bytes, clamping the final chunk. This is deterministic because `expected_size` is fixed at BEGIN.

*Trade-off accepted:* hash context becomes session state, so `sdf_ota_begin`/`sdf_ota_write`/`sdf_ota_abort` are all touched, and the change reaches the write path shared by all three trigger sources — a larger diff than the contained alternative, taken deliberately for correctness durability.

### D4. Extract a partition-independent `sdf_ota_verify_digest()`

```
sdf_ota_verify_digest(digest[32], sig[64], pubkey[65]) -> esp_err_t   // pure, no esp_partition
        ^
sdf_ota_verify_signature(partition, image_size)                       // reads footer, delegates
```

This is the decision that stops the rot. mbedTLS ECDSA builds for `IDF_TARGET=linux`, so `test_runner` can exercise `sdf_ota_verify_digest()` with NIST CAVP P-256 known-answer vectors plus a locally generated good/bad image pair — real cryptographic verification. Today's test only asserts that the *disabled* stub returns `ESP_OK`, which is why nothing caught defects 1–4. This was impossible under Ed25519 because no implementation existed to test against.

### D5. Default `CONFIG_SDF_OTA_SIGNATURE_VERIFY=y`; keep the symbol name

The symbol name is already algorithm-neutral; only its help text names Ed25519. Renaming would churn without benefit. Flipping the default to `y` is the point: **default `n` is precisely what let a fictional API survive in-tree for months.** Defaulting `y` means every build and CI run compiles the real path. Verification runs only on the OTA path, so plain `idf.py flash` is unaffected; the dev-workflow cost is confined to OTA testing, which needs signed images regardless.

### D6. Delete the `sdf_app.h` include rather than adding `sdf_app` to `REQUIRES`

`sdf_ota_signature.c` includes `sdf_app.h` but calls no `sdf_app_*` symbol — the include is vestigial. Deleting it resolves the spike's open question at zero cost. Adding `sdf_app` to `REQUIRES` would pull a hardware-bound component into `sdf_ota`'s dependency graph and re-break the linux build, which the spike explicitly warned against.

### D7. Regenerate the keypair as P-256

`openssl ecparam -genkey -name prime256v1` (or `ec.generate_private_key(ec.SECP256R1())`) replaces `genpkey -algorithm ed25519`; `extract-pubkey` emits 65 uncompressed bytes (`0x04 || X || Y`) instead of 32 raw. Uncompressed rather than compressed because point decompression is not reliably compiled into ESP-IDF's mbedTLS. Since no signed images and no field keys exist, this is a clean regeneration with no rotation ceremony. `sdf_sign_ota.py verify` remains the pre-deployment check, now hashing then verifying to mirror the device exactly.

## Confirmed Outcome: trailing-footer assumption

**Accepted.** `esp_ota_end()` accepts an image carrying 68 trailing bytes past the image proper, and the footer survives in flash byte-for-byte at `image_len`.

Verified two ways, source and experiment.

**Source.** `ota_verify_partition()` (`esp_ota_ops.c:527`) builds `esp_partition_pos_t` from `partition.staging->address` and `partition.staging->size` — the *partition* geometry, not `ota_ops->wrote_size`. `esp_image_verify()` then derives `image_len` by walking the header and summing segment lengths (`esp_image_format.c:581-606`), adds `HASH_LEN` for the appended SHA-256, and range-checks only `full_image_len > part_len` against the partition size (`esp_image_format.c:1033`). Bytes past `image_len` are never read and never contribute to any digest or length check.

**Experiment.** A standalone probe app (esp32c6, ESP-IDF v6.0.2) streamed its own running image into the other OTA slot via `esp_ota_write()` and called `esp_ota_end()`, run under the `esp-emu` RISC-V emulator (v0.38.0):

```
probe: case 'control_no_footer': image_len=184304 total=184304 -> ota_0
PROBE_RESULT control_no_footer ACCEPTED (ESP_OK)
probe: case 'with_footer': image_len=184304 total=184372 -> ota_0
PROBE_RESULT with_footer ACCEPTED (ESP_OK)
PROBE_FOOTER INTACT (magic=ok body=ok) bytes=a5 a5 a5 a5 ... 53 44 46 01
```

The control case pins that a rejection would have been attributable to the footer. The readback confirms the 64 signature bytes and the `SDF\x01` magic land exactly where `sdf_ota_verify_signature()` looks for them, at `image_len` and `image_len + 64`.

**Scope of the result.** Flash encryption was off, matching `firmware/sdkconfig.defaults`, which sets no `CONFIG_SECURE_FLASH_ENC_*` option. This does not weaken D3: the streaming digest is what makes the design correct *if* flash encryption is ever enabled, since `esp_ota_write()` would then defer the trailing `size % 16` bytes to `esp_ota_end()` and a read-back-at-commit digest would race that deferral. The probe confirms the footer format under the shipping configuration; D3 keeps it correct under the other one.

## Confirmed Outcome: end-to-end verification

Task 6 was executed against the `esp-emu` RISC-V emulator (v0.38.0) rather than a physical ESP32-C6. **Every result below is emulator-derived and none of it substitutes for a board bring-up.** What the emulator does give is the real `sdf_ota` component, compiled for esp32c6 with `CONFIG_SDF_OTA_SIGNATURE_VERIFY=y`, running the real `esp_ota_*` flash path against a real partition table — not host stubs.

The vehicle is a standalone probe app that links the shipping `sdf_ota` component and drives `sdf_ota_init/begin/write/verify_integrity/verify_and_commit` in 244-byte chunks, the size the BLE companion delivers. It streams its own running image into the other OTA slot, so the payload is a genuinely bootable, genuinely signed image. Its signature is produced out-of-band by `tools/sdf_sign_ota.py` and placed in a separate `testcfg` data partition together with the mode selector and the 65-byte public key, which avoids the circularity of embedding a signature inside the image it signs. The probe supplies its own `sdf_app_emit_audit()` so the audit stream is directly observable.

| Case | Result |
|---|---|
| signed | `Signature verification PASSED` → `OTA_COMMITTED` → reboot into `ota_1` at `0x1a0000` |
| tampered (one byte flipped mid-payload) | `Signature verification failed: -0x0095` (`MBEDTLS_ERR_ECP_VERIFY_FAILED`) → `OTA_SIGNATURE_INVALID` (status `0xA001`) → `OTA_FAILED`, not committed |
| unsigned (no footer, no magic) | `Signature magic marker not found` → `ESP_ERR_INVALID_CRC` → `OTA_SIGNATURE_INVALID` → `OTA_FAILED`, not committed |
| resume | sender stopped at 120,048 of 239,760 bytes, re-queried `sdf_ota_get_bytes_written()` (`device_confirmed=120048`), rewound to that offset and continued → `Signature verification PASSED` → committed |

The signed case was run to a second boot and observed to alternate `0x20000` ↔ `0x1a0000` across reboots, confirming the commit is real and the new slot is bootable rather than merely marked.

**What this does not cover.** The resume case exercises the offset-resume contract at the `sdf_ota` layer — the session and its digest accumulator surviving a gap and continuing from the device's confirmed offset — but *not* the BLE transport underneath it. A real GATT disconnect and reconnect was out of reach here: the full application cannot boot under the emulator, panicking in `rmt_tx_mark_eof` because the WS2812 RMT peripheral `sdf_drivers/src/led.c` drives is not modelled. Task 6.5 should still be repeated on hardware over an actual BLE link.

## Risks / Trade-offs

- [`esp_ota_end()` runs ESP-IDF's own `ota_verify_partition()` (`esp_ota_ops.c:594`) on an image carrying 68 trailing bytes past the image proper. ESP-IDF derives image length from the header segments, so trailing bytes *should* be ignored — but this is unproven against a real build, and "should be fine" reasoning is exactly what produced defects 1–4.] → Prove it early with a real signed image on hardware, before the rest of the work depends on it. This is sequenced as the first hardware task, not an assumption.
- [P-256 verification on ESP32-C6 has no hardware ECC acceleration and costs on the order of tens of ms, incurred inside `sdf_ota_verify_and_commit()` while holding `s_session_mutex`.] → One-time cost per update on a non-hot path; measure and record the actual figure rather than assuming. If it proves problematic, `mbedtls_ecdsa_verify_restartable()` exists as an escape hatch.
- [The digest change reaches `sdf_ota_write()`, shared by Zigbee, CLI, and BLE trigger paths, so a defect affects all three.] → The sequential-append property that makes this safe is verified (D3) but should be pinned by a host test asserting the hash advances exactly once per byte across a chunked write sequence.
- [Flipping the default to `y` means any developer OTA flow now requires correctly signed images; an unsigned image that previously "worked" will now be rejected.] → Intended behavior, and the reason the flag exists. `ota-key-autogen` already generates a key automatically at build time, so the local path stays a one-command signing step.
- [Regenerating the keypair invalidates any local `ota_private.key`.] → No signed images or field keys exist; developers regenerate automatically via the existing autogen path.

## Migration Plan

1. Prove the trailing-footer assumption on hardware with a signed image (blocking gate, see Risks).
2. Land the P-256 verification core and host tests with known-answer vectors — verifiable without touching the write path.
3. Wire the streaming digest into `sdf_ota_write()`/`sdf_ota_begin()`/`sdf_ota_abort()`.
4. Update `sdf_sign_ota.py` and key generation to P-256; regenerate keys.
5. Flip `CONFIG_SDF_OTA_SIGNATURE_VERIFY` to `y` once an end-to-end signed OTA has succeeded on hardware.
6. Correct `AGENTS.md:71` and the Kconfig help text.

**Rollback:** revert to `CONFIG_SDF_OTA_SIGNATURE_VERIFY=n`, which restores the no-op stub and today's (non-)behavior. No persistent device state or data migration is involved. Because nothing is signed or deployed today, there is no field population to strand.

## Open Questions

- ~~Exact P-256 verification latency on ESP32-C6~~ — **answered, restartable not warranted.** Measured under the `esp-emu` RISC-V emulator (v0.38.0, esp32c6, ESP-IDF v6.0.2), timing `sdf_ota_verify_digest()` in isolation with `esp_timer_get_time()`:

  | Step | Time | Notes |
  |---|---|---|
  | `sdf_ota_verify_digest()` (ECDSA P-256 verify) | **28.0 ms** | stable to ±2 µs across four runs |
  | SHA-256 over a 239,760-byte image | 112.3 ms | for scale; already amortized across `sdf_ota_write()` calls |
  | `sdf_ota_verify_and_commit()`, rejecting a tampered image | 28.5 ms | i.e. the ECDSA verify plus ~0.5 ms of footer read and version check |

  **Caveat: these are emulator figures, not silicon figures.** `esp-emu` does not claim cycle accuracy, and the C6 has no ECC accelerator that mbedTLS would use here, so treat 28 ms as an order-of-magnitude result to be confirmed on a real board before it is quoted anywhere load-bearing.

  `mbedtls_ecdsa_verify_restartable()` is **not** warranted. The verification is a single 28 ms call on a dedicated OTA path, made once per update after the transfer has already finished — there is no BLE connection interval to hold open and nothing else waiting on the CPU at that moment. The streaming digest already removed the part that scaled with image size; what remains is a fixed cost that does not grow, and splitting it into restart-driven slices would add state-machine complexity for no user-visible gain. Revisit only if on-device measurement comes back an order of magnitude worse than this, or if a watchdog with a sub-100 ms budget is ever introduced on the committing task.
- Whether CI should hold a dedicated signing key (injected via secrets, per `ota-key-autogen`'s existing "CI builds firmware with injected key" scenario) or generate an ephemeral one per run. Existing autogen behavior likely suffices; confirm when wiring `firmware-ci`.
- Whether `sdf_ota.c`'s partition-write/rollback logic should finally get host coverage via a `sdf_app_emit_audit` linux stub. Out of scope here, but the streaming digest now living in that file strengthens the case — worth revisiting as a follow-up.
