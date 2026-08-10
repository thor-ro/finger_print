## 1. Resolve the validation vehicle (blocking gate for task 5)

- [x] 1.1 Determine whether `esp-emu` v0.38.0 models XTS-AES flash encryption, or stubs `esp_efuse_is_flash_encryption_enabled()` / decryption to a pass-through
- [x] 1.2 If it does not model it faithfully, define the fallback vehicle before writing code — e.g. a probe that forces the deferral path and asserts on `partial_bytes` behavior directly — and record the choice in design.md under a "Confirmed Outcome" section
- [x] 1.3 Confirm on the current `main` that the chosen vehicle **reproduces the failure** (`Signature magic marker not found`) against unmodified firmware. A hardening change with no failing baseline is untested hardening

## 2. Window capture primitive

- [x] 2.1 Add `SDF_OTA_APP_DESC_OFFSET` (32) and `SDF_OTA_APP_DESC_SIZE` (256) to `firmware/components/sdf_ota/include/sdf_ota.h`, with a static assert tying `SDF_OTA_APP_DESC_SIZE` to `sizeof(esp_app_desc_t)` and a comment citing `esp_ota_ops.c:985` as the source of the offset
- [x] 2.2 Declare `sdf_ota_window_capture(uint8_t *dst, uint32_t win_start, uint32_t win_len, uint32_t stream_offset, const void *data, uint32_t len)` in `include/sdf_ota_digest.h`, taking no partition, flash, or session argument
- [x] 2.3 Implement it in `src/sdf_ota_signature.c` alongside the digest accumulator, outside every config guard so the host build sees it
- [x] 2.4 Add host tests in `firmware/components/sdf_ota/test/`: a window captured from a stream split at **every** boundary yields byte-identical results; chunks wholly before or after the window leave `dst` untouched; a window at the very end of the stream and a window spanning three chunks both capture correctly; `len == 0` and zero-length windows are no-ops
- [x] 2.5 Confirm the new tests build and pass under `IDF_TARGET=linux` via `cd firmware/test_runner && rtk idf.py build && ./build/sdf_test_runner.elf`

## 3. Verification entry point

- [x] 3.1 Add `sdf_ota_verify_footer(const uint8_t footer[SDF_OTA_FOOTER_SIZE], const uint8_t digest[SDF_OTA_DIGEST_SIZE])` to `include/sdf_ota.h`
- [x] 3.2 Implement it in `src/sdf_ota_signature.c`: magic-marker check returning `ESP_ERR_INVALID_CRC` on mismatch, then delegate to `sdf_ota_verify_digest()` with the embedded public key
- [x] 3.3 Reimplement `sdf_ota_verify_signature(partition, image_size, digest)` as "read the 68-byte footer from flash, delegate to `sdf_ota_verify_footer()`" — signature unchanged, magic-check logic not duplicated
- [x] 3.4 Add the `#else` stub for `sdf_ota_verify_footer()` matching the existing disabled/linux branch convention
- [x] 3.5 Confirm `sdf_cli_commands.c:976` still compiles and behaves identically — the out-of-band `ota verify` path is deliberately unchanged

## 4. Session: stream capture and handle ownership

- [x] 4.1 Add `uint8_t footer[SDF_OTA_FOOTER_SIZE]`, `uint8_t app_desc[SDF_OTA_APP_DESC_SIZE]`, and `bool ota_handle_open` to `sdf_ota_session_t` in `src/sdf_ota.c`
- [x] 4.2 Tighten `sdf_ota_begin()`'s size guard to reject `image_size < SDF_OTA_APP_DESC_OFFSET + SDF_OTA_APP_DESC_SIZE + SDF_OTA_FOOTER_SIZE`, with an error message naming the minimum; set `ota_handle_open = true` after a successful `esp_ota_begin()`
- [x] 4.3 In `sdf_ota_write()`, capture both windows off `s_session.bytes_written` before it is advanced, alongside the existing `sdf_ota_digest_update()`
- [x] 4.4 Add `sdf_ota_session_fail(esp_err_t err)` — releases the digest, aborts the handle only if `ota_handle_open`, transitions to `FAILED`, clears `active`, returns `err`
- [x] 4.5 Route every failure path in `sdf_ota_write()`, `sdf_ota_verify_integrity()`, and `sdf_ota_verify_and_commit()` through it, replacing the hand-rolled digest-release-plus-transition blocks
- [x] 4.6 Clear `ota_handle_open` immediately after `esp_ota_end()` returns, on **both** the success and failure branches, before any `sdf_ota_session_fail()` call — `esp_ota_end()` frees its entry on every path (`esp_ota_ops.c:604-610`)
- [x] 4.7 Replace the version check's `esp_ota_get_partition_description(s_session.target_partition, &target_desc)` with a read of the captured `app_desc`, including the `ESP_APP_DESC_MAGIC_WORD` check that `esp_ota_get_partition_description()` performed. Leave the *running* partition's description read alone — that partition is always fully committed
- [x] 4.8 Replace `sdf_ota_verify_signature(s_session.target_partition, ...)` with `sdf_ota_verify_footer(s_session.footer, digest)`
- [x] 4.9 Grep `src/sdf_ota.c` for remaining uses of `s_session.target_partition` and confirm the only survivor is the `esp_ota_set_boot_partition()` call after `esp_ota_end()`
- [x] 4.10 Update the session-struct comment (`sdf_ota.c:30-36`) so it describes all three stream-captured windows, not just the digest

## 5. Validation

- [x] 5.1 Build for esp32c6 with `CONFIG_SDF_OTA_SIGNATURE_VERIFY=y` and confirm no size or link regressions
- [x] 5.2 Re-run the P-256 change's four end-to-end cases (signed, tampered, unsigned, resume) via the existing probe app and confirm identical outcomes
- [x] 5.3a Using the vehicle chosen in task 1, run a signed OTA **with flash encryption enabled** and confirm everything before `esp_ota_end()` succeeds — the half that fails on current `main`. (The commit itself is not reachable under `--efuse`: `esp_ota_end()` fails inside IDF for emulator-model reasons — see design.md "Confirmed Outcome: the validation vehicle".)
- [x] 5.3b Run the same signed OTA with flash encryption **off** and confirm it commits and reboots into the new slot
- [x] 5.4 Force a commit failure on the Zigbee path (tampered image) and confirm a subsequent `sdf_ota_begin()` from any transport succeeds without a reboot — the wedge described in the proposal
- [x] 5.5 Confirm a redundant `sdf_ota_abort()` after a failed commit returns `ESP_ERR_INVALID_STATE` and does not double-abort
- [x] 5.6 Record every result in design.md under "Confirmed Outcome", stating plainly which were emulator-derived and which were not
- [x] 5.7 Repeat 5.2 and 5.3 on physical hardware over a real BLE link, or record explicitly that it was not done and why

## 6. Callers and documentation

- [x] 6.1 Null `s_ota_session` on the integrity-failure path in `sdf_protocol_zigbee.c:672-676`, which currently leaves it pointing at a session it never closes
- [x] 6.2 Leave `sdf_ble_companion_ota.c:256`'s `sdf_ota_abort()` in place — now redundant, still correct — and note in the comment above it that release is the component's responsibility
- [x] 6.3 Amend the "Scope of the result" paragraph in `openspec/changes/archive/2026-08-10-replace-ota-signature-with-p256/design.md` to scope its correctness claim to the digest, with a pointer to this change for the footer and descriptor reads
- [x] 6.4 Run `rtk cargo clippy`-equivalent gates for this repo (`rtk idf.py build` for esp32c6 and linux, plus the host suite) and confirm no new warnings
- [x] 6.5 `openspec validate harden-ota-commit-path --strict`
