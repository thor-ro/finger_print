## Why

`sdf_ota_verify_and_commit()` reads the target partition **before** `esp_ota_end()`, in two places: the 68-byte signature footer (`sdf_ota_signature.c:267`) and the incoming version's `esp_app_desc_t` (`sdf_ota.c:350`). Both reads are only correct because of ESP-IDF behavior that is not guaranteed and is one config flag away from changing.

**The live hazard — flash encryption.** `esp_ota_write()` defers the trailing `size % 16` bytes into an internal buffer and only flushes them in `esp_ota_end()`, gated on `esp_efuse_is_flash_encryption_enabled()` (`esp_ota_ops.c:347-377` defer, `:583-592` flush). ESP app images are 16-byte aligned by construction, and `68 % 16 == 4`, so the deferred slice is *exactly* the 4-byte `SDF\x01` magic. The moment `CONFIG_SECURE_FLASH_ENC_ENABLED` is turned on, `sdf_ota_verify_signature()` reads erased flash through XTS-AES decryption at that offset, the magic check fails first, and **every OTA fails on the first attempt** with:

```
E sdf_ota_sig: Signature magic marker not found (image not signed?)  -> ESP_ERR_INVALID_CRC
```

That message points the operator at `sdf_sign_ota.py` and the key material — nowhere near the actual cause. This is not a subtle heisenbug: it is a deterministic, total, permanently-OTA-blocking failure wearing a misleading label. It is not active today (`firmware/sdkconfig.defaults` sets no `CONFIG_SECURE_FLASH_ENC_*`; `firmware/sdkconfig:872` has it unset), but this is a door lock with `CONFIG_SECURE_BOOT_V2_PREFERRED=y` and full flash-encryption SOC support already in config.

The archived `2026-08-10-replace-ota-signature-with-p256` design **identified this exact deferral** (D3) and built the streaming digest specifically to survive it — but the reasoning stopped at the digest. Its "Scope of the result" section then claims "D3 keeps it correct under the other one," which is too strong: the footer read is the same hazard's unaddressed residual, inside the very function D3 was defending. Left as-is, that sentence reads to a future maintainer as a clean bill of health.

**The latent trap — staging partitions.** ESP-IDF v6 added `esp_ota_set_final_partition(handle, final, finalize_with_copy)` (`esp_ota_ops.c:262`): with it, writes land in a *staging* partition and are copied to the final partition inside `esp_ota_end()` (`:598-601`). `sdf_ota` never calls it, so `staging == final` and this is inert today — but any future bootloader or partition-table OTA would make every pre-`esp_ota_end()` read of `s_session.target_partition` return the **entire previous image**, not four stale bytes. The version check would compare the running version against the image being replaced.

**The handle leak.** No failure path in `sdf_ota.c` releases the `esp_ota_handle_t` or clears `s_session.active`; each one leaves both outstanding and relies on the caller to call `sdf_ota_abort()`. The BLE companion does (`sdf_ble_companion_ota.c:256`). **The Zigbee handler does not** — `sdf_protocol_zigbee.c:688-691` logs the error and sets `s_ota_session = NULL`. Since `sdf_ota_begin()` refuses to start while `s_session.active` is true (`sdf_ota.c:146`), one failed Zigbee OTA leaks an `ota_ops_entry_t` and **wedges OTA on every transport until reboot**. The same gap exists in the `sdf_ota_write()` and `sdf_ota_verify_integrity()` failure paths.

## What Changes

- **Capture the signature footer from the write stream** instead of reading it back from flash. `expected_size` is fixed at BEGIN, so the footer window `[expected_size - 68, expected_size)` is known before the first byte arrives — the exact complement of the digest window the accumulator already clamps at.
- **Capture the 256-byte `esp_app_desc_t` from the write stream** (window `[32, 288)`, per `esp_ota_get_partition_description()`'s own `esp_partition_read(partition, sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t), ...)`) and run the version check against the captured copy.
- **Net effect: `sdf_ota_verify_and_commit()` performs zero reads of the target partition before `esp_ota_end()`.** Immune to the ≤15-byte deferral, to staging-partition indirection, to transparent-decryption semantics, and to any future change in `esp_ota_write()` buffering — the same durability argument D3 already made for the digest, now applied consistently.
- **Add `sdf_ota_window_capture()`**, one pure, host-testable primitive used three times (digest range, app-desc window, footer window). The chunk-boundary arithmetic is the part most likely to carry an off-by-one, and it becomes exhaustively testable on `IDF_TARGET=linux`.
- **Add `sdf_ota_verify_footer(footer[68], digest[32])`** — magic check plus delegation to `sdf_ota_verify_digest()` with the embedded key. `sdf_ota_verify_signature(partition, image_size, digest)` keeps its signature and is reimplemented as "read footer from flash, delegate to `sdf_ota_verify_footer()`", so the out-of-band `sdf_cli` `ota verify` path (which runs post-commit, where a flash read is correct) is unchanged and no logic is duplicated.
- **Make every `sdf_ota` failure path self-contained.** A single internal `sdf_ota_session_fail()` releases the digest, aborts the `esp_ota_handle_t` if still open, transitions to `FAILED`, and clears `active`. After any non-`ESP_OK` return, no handle is outstanding and `sdf_ota_begin()` succeeds again. Callers' existing `sdf_ota_abort()` becomes a harmless `ESP_ERR_INVALID_STATE` no-op.
- **Track handle ownership explicitly** with `s_session.ota_handle_open`, because `esp_ota_end()` frees the entry on *every* path including failure (`esp_ota_ops.c:604-610`) — so the `esp_ota_end()` and `esp_ota_set_boot_partition()` failure paths must clear state without calling `esp_ota_abort()`.
- **Null the Zigbee session handle on integrity failure** (`sdf_protocol_zigbee.c:672-676`), which today leaves `s_ota_session` pointing at a session it never closes.
- **Correct the archived P-256 design's "Scope of the result" claim** so it no longer reads as a clean bill of health for flash encryption.

## Capabilities

### New Capabilities
- `ota-session-lifecycle`: OTA session state ownership — when a session is active, who owns the `esp_ota_handle_t`, and the guarantee that a failed session releases everything it holds. Listed as new because it is **absent from `openspec/specs/`**: the 2026-07-23 `ota-mechanism` change specified the state machine but its deltas were never fully synced to main specs, which is the same gap that let the P-256 breakage sit unnoticed.

### Modified Capabilities
- `ota-signature`: "Verification Occurs In-App Before Commit" is strengthened — verification not only precedes `esp_ota_end()`, it must not depend on reading back what `esp_ota_write()` has (or has not yet) flushed. "Streaming Digest Computation" gains the parallel guarantee for the footer and app descriptor.
- `firmware-host-test-runner`: gains coverage of `sdf_ota_window_capture()` chunk-boundary behavior.

## Impact

- **Firmware**: `firmware/components/sdf_ota/src/sdf_ota.c` (session gains `footer[68]`, `app_desc[256]`, `ota_handle_open`; `sdf_ota_write()` feeds three windows; `verify_and_commit()` stops touching the partition; all failure paths routed through one helper), `src/sdf_ota_signature.c` (`sdf_ota_window_capture()`, `sdf_ota_verify_footer()`, `sdf_ota_verify_signature()` refactored to delegate), `include/sdf_ota.h`, `include/sdf_ota_digest.h`.
- **RAM**: +325 bytes of static BSS in `s_session` (68 + 256 + 1, plus padding). No heap.
- **Callers**: `sdf_protocol_zigbee.c` (one line, integrity-failure path). `sdf_ble_companion_ota.c` unchanged — its `sdf_ota_abort()` becomes redundant but stays correct.
- **Tests**: `firmware/components/sdf_ota/test/` gains window-capture tests; `firmware/test_runner` wiring.
- **Docs**: `openspec/changes/archive/2026-08-10-replace-ota-signature-with-p256/design.md` "Scope of the result" paragraph corrected with a forward pointer to this change.
- **Not affected**: signature algorithm, footer format, wire protocol (BEGIN/CHUNK/END, `chunk_ack` offsets), the web companion, version-comparison semantics, rollback behavior, `sdf_cli`'s `ota verify`, and the embedded key.
- **Not in scope**: actually enabling `CONFIG_SECURE_FLASH_ENC_ENABLED`. This change makes the OTA path *ready* for that decision; taking it is a separate one with its own eFuse-burning, key-management, and no-going-back consequences.
