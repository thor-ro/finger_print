## Context

Three defects share one root cause: `sdf_ota` treats the target partition as a readable source of truth while an `esp_ota_handle_t` is still open, and treats the handle as the caller's problem when things fail.

The bytes `sdf_ota_verify_and_commit()` needs before `esp_ota_end()` are:

```
stream offset   0        32      288                         signed_len      expected_size
                │        │        │                              │                 │
                ├────────┼────────┼──────────────────────────────┼─────────────────┤
                │        │app_desc│                              │  sig[64] magic[4]│
                │        └────────┘                              └─────────────────┘
                └───────── digest window [0, signed_len) ────────┘

   read today from:  flash (esp_ota_get_partition_description)   flash (esp_partition_read)
   available from:   the write stream                            the write stream
```

The digest already comes from the stream — D3 of the archived P-256 change made that choice deliberately, precisely to dodge `esp_ota_write()`'s trailing-byte deferral. The other two windows were left on flash. This change finishes that thought.

Ground truth, ESP-IDF v6.0.2 (`~/.espressif/v6.0.2/esp-idf/components/app_update/esp_ota_ops.c`):

| Behavior | Location | Consequence for a pre-`esp_ota_end()` read |
|---|---|---|
| Defers `size % 16` trailing bytes when flash encryption is on | `:347-377` (stash), `:583-592` (flush) | last `total % 16` bytes of the image are erased flash |
| Frees the ops entry on **every** `esp_ota_end()` path, including failure | `:604-610` | a later `esp_ota_abort()` gets `ESP_ERR_NOT_FOUND` |
| `esp_ota_abort()` is a plain lookup-then-free | `:440-450` | double-abort is not a UAF, but is not a no-op either |
| Staging → final copy happens inside `esp_ota_end()` | `:262` (opt-in), `:598-601` (copy) | inert today; would return the *previous* image wholesale |
| `esp_ota_get_partition_description()` reads at offset 32, length 256 | `:985` | a flash read like any other, subject to all of the above |

`sizeof(esp_app_desc_t) == 256` is static-asserted by IDF (`esp_app_desc.h:44`), and its offset is `sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t)` = 24 + 8 = 32. Both are stable, load-bearing parts of the image format — the bootloader depends on them.

## Goals / Non-Goals

**Goals:**
- `sdf_ota_verify_and_commit()` performs zero reads of `s_session.target_partition` before `esp_ota_end()`.
- Correctness independent of `esp_ota_write()` buffering semantics, flash encryption state, and staging-partition indirection — not "correct given the current config."
- A failed `sdf_ota` operation leaves no `esp_ota_handle_t` outstanding and no wedged session, regardless of what the caller does next.
- The chunk-boundary arithmetic is host-testable.

**Non-Goals:**
- Enabling flash encryption or Secure Boot v2. This change removes a blocker; it does not take that decision.
- Changing the signature algorithm, footer format, key, or wire protocol.
- Changing `sdf_cli`'s `ota verify`, which reads a *committed* partition out-of-band and where a flash read is the correct and only option.
- Making `sdf_ota.c` as a whole host-testable — still blocked by its `sdf_app_emit_audit` link dependency, unchanged from the P-256 design's non-goals. Only the new pure helper becomes host-testable.
- Anti-rollback / minimum-version enforcement.

## Decisions

### D1. Capture windows from the write stream; do not read the partition mid-session

`sdf_ota_write()` gains two capture calls alongside the existing digest update, all three driven off `s_session.bytes_written` as the stream offset:

```c
sdf_ota_window_capture(s_session.app_desc, SDF_OTA_APP_DESC_OFFSET, SDF_OTA_APP_DESC_SIZE,
                       s_session.bytes_written, data, len);
sdf_ota_window_capture(s_session.footer, s_session.expected_size - SDF_OTA_FOOTER_SIZE,
                       SDF_OTA_FOOTER_SIZE, s_session.bytes_written, data, len);
sdf_ota_digest_update(&s_session.digest, data, len);   /* unchanged */
```

Order matters only in that all three run *before* `bytes_written += len`.

*Why this over the alternatives:*

| | Reorder `esp_ota_end()` before verify | **Capture from stream (chosen)** |
|---|---|---|
| Diff | ~3 lines moved | ~15 lines + one helper |
| Partition reads before `esp_ota_end()` | still 2, now merely correct ones | **0** |
| Fixes the deferral hazard | yes | yes |
| Fixes the staging hazard | yes | yes |
| **Rejects a bad image *before* it is copied to the final partition (staging case)** | **no** | **yes** |
| Survives future IDF buffering changes | no — re-establishes the dependency, just later | yes — no dependency to survive |
| Failure-path fallout | `esp_ota_end()` frees the entry, so callers' `sdf_ota_abort()` starts returning `ESP_ERR_NOT_FOUND` — needs the D3 handle tracking anyway | none |
| Source-of-truth coherence | digest from stream, footer from flash — split | all three from one source |

The reorder is genuinely cheaper, fixes both hazards, and was considered seriously. It loses on the bolded row. `esp_ota_end()` is not a passive flush: it runs `ota_verify_partition()` and, under `finalize_with_copy`, copies the staged image over the final partition (`esp_ota_ops.c:594-601`). Verifying *after* it means an unsigned, attacker-supplied image has already overwritten the final partition by the time the trust decision is made. The boot partition is still unchanged, so this is not immediately exploitable — but "reject before anything irreversible happens to the target" is the property the existing `Verification Occurs In-App Before Commit` requirement is reaching for, and the reorder trades it away to save a dozen lines.

Beyond that: the reorder doesn't remove the flash dependency, it moves it to a point where it currently happens to be safe — which is the same shape of reasoning that produced this change's `Why`. Stream capture removes the dependency. Add that a) the digest already established stream capture as the pattern here, b) the reorder needs the D3 handle-lifecycle work regardless, and c) the marginal cost is one pure function, and the consistent option wins on durability per line of diff.

**Objection: "you're no longer verifying what actually landed in flash."** That property is already gone — the digest has been stream-derived since the P-256 change, and it is the digest that carries the cryptographic weight. It was never the real protection either: `esp_ota_end()` runs IDF's own `ota_verify_partition()` SHA-256 check over what is genuinely in flash (`esp_ota_ops.c:594`), and an attacker who can corrupt flash between write and read already owns the device. Stream capture makes the two halves of the verification agree on their source instead of straddling two.

### D2. One pure window primitive, three uses

```c
/* Copies the intersection of [win_start, win_start+win_len) with
 * [stream_offset, stream_offset+len) into dst at the appropriate offset.
 * No state, no partial-window bookkeeping, idempotent per byte. */
void sdf_ota_window_capture(uint8_t *dst, uint32_t win_start, uint32_t win_len,
                            uint32_t stream_offset, const void *data, uint32_t len);
```

It lives in `sdf_ota_signature.c` next to the digest accumulator — deliberately, because that file already builds for `IDF_TARGET=linux` while `sdf_ota.c` does not. Extracting it is what makes the chunk-boundary arithmetic exhaustively testable on the host, and chunk-boundary arithmetic is exactly where an off-by-one would hide: BLE delivers 244-byte chunks, so the 256-byte app-desc window at offset 32 straddles chunks 0 and 1, and the 68-byte footer window lands wherever `expected_size % 244` puts it.

It is deliberately *not* a sliding window over the last N bytes. `expected_size` is fixed at BEGIN, so absolute windows are exact and need no ring-buffer state. Same reasoning that lets the digest clamp at `signed_len` without splitting chunks.

Both windows are zero-initialized at BEGIN and, like the digest, depend on writes being a pure sequential append. That property is already established and verified (P-256 design D3: no offset addressing in `sdf_ota_write()`, transports resume from the device's own confirmed `bytes_written`), so a resumed transfer captures each window byte exactly once, in order.

### D3. Explicit handle ownership, and one failure helper

Add `bool ota_handle_open` to the session. True from a successful `esp_ota_begin()` until whichever comes first: `esp_ota_abort()`, or `esp_ota_end()` returning *any* value.

That last clause is the subtle one. `esp_ota_end()` frees the ops entry through its `cleanup:` label on every path including `wrote_size == 0`, verification failure, and copy failure (`esp_ota_ops.c:604-610`). So the `esp_ota_end()`-failed and `esp_ota_set_boot_partition()`-failed branches must clear session state **without** calling `esp_ota_abort()` — the flag is what encodes that, rather than a comment nobody reads.

```c
static esp_err_t sdf_ota_session_fail(esp_err_t err)   /* caller holds s_session_mutex */
{
    sdf_ota_session_digest_release();
    if (s_session.ota_handle_open) {
        esp_ota_abort(s_session.ota_handle);
        s_session.ota_handle_open = false;
    }
    sdf_ota_state_transition(SDF_OTA_STATE_FAILED);
    s_session.active = false;
    return err;
}
```

Every failure path in `sdf_ota_write()`, `sdf_ota_verify_integrity()`, and `sdf_ota_verify_and_commit()` routes through it. Today each of those hand-rolls a subset — digest release plus a `FAILED` transition — and none of them clears `active` or touches the handle.

The resulting caller contract is *idempotent close*: after any non-`ESP_OK` return, the session is closed and holds nothing. A caller that then calls `sdf_ota_abort()` gets `ESP_ERR_INVALID_STATE` from the existing `!s_session.active` guard (`sdf_ota.c:273`) and does no harm. This is strictly better than requiring callers to clean up, because one of the two callers doesn't.

*Alternative considered — fix the Zigbee caller instead.* Rejected: it makes the contract "every caller must abort on every failure," which is unenforceable, already violated once, and violated silently. Ownership belongs with the component that opened the handle.

### D4. `sdf_ota_verify_footer()` as the shared verification entry point

```
sdf_ota_verify_digest(digest[32], sig[64], pubkey[65])      pure, host-testable, unchanged
        ▲
sdf_ota_verify_footer(footer[68], digest[32])               magic check + embedded key
        ▲                                    ▲
   sdf_ota.c (session)                sdf_ota_verify_signature(partition, size, digest)
   footer from stream                 footer from flash — sdf_cli `ota verify` only
```

`sdf_ota_verify_signature()` keeps its exact current signature and becomes a two-liner. `sdf_cli_commands.c:976` is untouched, and its flash read stays correct because it runs post-commit on a partition with no open handle. The magic-marker check and the `ESP_ERR_INVALID_CRC` "not signed" convention move down into `sdf_ota_verify_footer()` and are not duplicated.

`sdf_ota_verify_footer()` stays behind `#if CONFIG_SDF_OTA_SIGNATURE_VERIFY && !CONFIG_IDF_TARGET_LINUX` like its sibling, since it depends on the build-embedded key blob; the `#else` branch gets the matching `return ESP_OK` stub. The host-testable surface remains `sdf_ota_verify_digest()` plus, new here, `sdf_ota_window_capture()`.

### D5. Reject images too small to carry the app descriptor

`sdf_ota_begin()` already rejects `image_size <= SDF_OTA_FOOTER_SIZE`. Tighten to `image_size < SDF_OTA_APP_DESC_OFFSET + SDF_OTA_APP_DESC_SIZE + SDF_OTA_FOOTER_SIZE` (356 bytes), which guarantees both windows are fully populated and non-overlapping. Anything smaller cannot be a valid ESP app image — `esp_ota_write()` would reject it at the magic byte anyway (`esp_ota_ops.c:334`) — so this only moves an inevitable rejection earlier, to a place with a clear error message.

Without the guard, a short image leaves the app-desc window partly zero, `magic_word` fails to match `ESP_APP_DESC_MAGIC_WORD`, and the version check reports `ESP_ERR_NOT_FOUND` — correct, but from a confusing distance.

### D6. Correct the archived P-256 design rather than only superseding it

`archive/2026-08-10-replace-ota-signature-with-p256/design.md`, "Scope of the result", asserts that D3 "keeps it correct under the other one [flash encryption enabled]." That was true of the digest and false of the function as a whole. Archived designs are read as settled findings; leaving a known-wrong safety claim in one is how the next person concludes this ground is already covered — the same failure mode that let a fictional Ed25519 API survive in-tree for months.

Amend the paragraph in place with a scoped correction and a pointer here. Not a rewrite of history: the finding it records is sound, its scope was overstated.

## Confirmed Outcome: the validation vehicle

**`esp-emu` v0.38.0 models the eFuse gate faithfully and the encrypted flash data path unfaithfully.** Both halves matter, and they split task 5 in two.

The emulator ships `src/periph/efuse.rs` and `src/periph/xts_aes.rs`, and takes a QEMU-compatible 336-byte eFuse image via `--efuse`. For esp32c6 that image is `BLK0` (6 words) + `BLK1` (6 words) + `BLK2..BLK10` (8 words each) = 336 bytes, so `SPI_BOOT_CRYPT_CNT` (`esp_efuse_table.csv:126` — `EFUSE_BLK0`, bit 82, 3 bits) lands at **byte 10, bit 2**:

```python
b = bytearray(336); b[10] |= 0x04          # SPI_BOOT_CRYPT_CNT = 1 -> enabled
```

The emulator confirms it on load — `eFuse loaded from binary: MAC=…, crypt_cnt=1, key_purposes=[0, …]` — and `esp_efuse_is_flash_encryption_enabled()` returns 1 inside the guest. That is the *only* thing gating `esp_ota_write()`'s deferral (`esp_ota_ops.c:347`), so the deferral is reproduced exactly, with no stubbing on our side.

**Vehicle (task 1.2).** A standalone esp32c6 probe app that streams its own running image plus a 68-byte `0xA5…SDF\x01` footer into the other OTA slot in 244-byte chunks — the size the BLE companion delivers — and then reads the footer back from the target partition at precisely the point `sdf_ota_verify_signature()` does today: after the last `esp_ota_write()`, before `esp_ota_end()`. Run twice against the same binary, once without `--efuse` and once with the blob above. As with the P-256 change, the probe is not checked in.

**Baseline reproduced (task 1.3), against unmodified `main`:**

```
--- no --efuse ---------------------------------------------------
PROBE_ENC efuse_is_flash_encryption_enabled=0
PROBE_ENC image_len=188160 total=188228 total%16=4
PROBE_ENC footer_pre_end tail= a5 a5 a5 a5 53 44 46 01
PROBE_ENC PRE_END magic=OK

--- --efuse efuse_enc.bin (SPI_BOOT_CRYPT_CNT=1) ------------------
PROBE_ENC efuse_is_flash_encryption_enabled=1
PROBE_ENC image_len=188160 total=188228 total%16=4
PROBE_ENC footer_pre_end tail= a5 a5 a5 a5 ff ff ff ff
PROBE_ENC PRE_END magic=MISSING
PROBE_ENC POST_END magic=OK
```

`total % 16 == 4` as predicted, and the four missing bytes are exactly the `SDF\x01` magic — erased flash before `esp_ota_end()`, present after it. This is the `Signature magic marker not found` failure, isolated to its cause. There is a failing baseline.

**Where the emulator stops.** With `crypt_cnt=1` the emulator's encrypted flash path is not a faithful round trip: comparing the target partition against the running image it was streamed from reports `body_mismatches=26929 of 188160`, first at offset 224 (`running=00 target=b4`, and `00` is the true byte — `enc_probe.bin[224]`). `esp_ota_end()` consequently fails inside IDF's own `ota_verify_partition()`:

```
E esp_image: Checksum failed. Calculated 0x3e read 0x4f
E esp_ota_ops: New image failed verification
PROBE_ENC esp_ota_end: ESP_ERR_OTA_VALIDATE_FAILED
```

On silicon that round trip is faithful by construction (`esp_flash_write_encrypted` / `esp_flash_read_encrypted` are inverses), so this is an emulator artifact, not a finding about `sdf_ota`. But it is load-bearing for what task 5 can claim: **no amount of work in `sdf_ota` makes a commit succeed under `--efuse` in this emulator, because the failure is inside `esp_ota_end()` and upstream of us.** Task 5.3 is therefore split:

- **5.3a — emulator, encryption on.** Everything `sdf_ota_verify_and_commit()` does *before* `esp_ota_end()` succeeds: the magic marker is found, the signature verifies, the version check reads a well-formed descriptor. This is the half the change is about, and the half that fails today.
- **5.3b — emulator, encryption off.** The commit completes and reboots into the new slot, as in the P-256 change.

The one configuration neither run covers — a real commit under a real deferral — is left to task 5.7 on hardware, and is recorded there rather than implied.

### Results (tasks 5.2–5.5, 2026-08-10)

The task-1 probe above exercised the raw `esp_ota_*` sequence to isolate the deferral. Task 5 needed a second, separate probe driving the real, just-written `sdf_ota` public API (`sdf_ota_init/begin/write/verify_integrity/verify_and_commit`) end to end, so the evidence covers the code in this change rather than a hand-rolled equivalent. It links the real `sdf_ota` + `sdf_common` against a stub `sdf_config` (the real one drags in `sdf_platform` + `sdf_protocol_zigbee` for one vestigial include), streams the running image plus a `tools/sdf_sign_ota.py`-produced footer through the API in 244-byte chunks (matching the BLE companion), and covers four modes selected by a `testcfg` data partition flashed independently of the signed app image (avoiding the build→sign→embed circularity): `SIGNED`, `TAMPERED` (one flipped body byte, real footer), `UNSIGNED` (zeroed footer), `RESUME` (real footer, queries `sdf_ota_get_bytes_written()` mid-stream). Not checked in, per the same convention as the task-1 probe.

**A methodological error and its fix.** The first pass at this probe stubbed `sdf_config` down to an empty header, missing that `CONFIG_SDF_OTA_SIGNATURE_VERIFY` (and `_ALLOW_DOWNGRADE`, `_BOOTLOADER_ROLLBACK`) are defined in *`sdf_config`'s* `Kconfig`, not `sdf_ota`'s own (`components/sdf_ota/Kconfig` is a one-line pointer there). With the stub in place the option was never defined, so `#if CONFIG_SDF_OTA_SIGNATURE_VERIFY` in `sdf_ota_verify_and_commit()` silently compiled to `0` — the build succeeded, the binary was smaller (203376 vs. 230304 bytes once fixed), and 748 consecutive SIGNED-mode commit/reboot cycles "passed" without ever calling `sdf_ota_verify_footer()`. The TAMPERED-mode run in that pass was also invalid: it failed, but at `esp_ota_end()`'s own unrelated image checksum, not at signature verification. Caught before any task was marked done by noticing `grep -n "Signature verification passed"` matched nothing in either log despite the config appearing set in `sdkconfig.defaults`. Fixed by adding a matching `Kconfig` (same three options, same defaults) to the stub `sdf_config` component; `sdkconfig` and `sdkconfig.h` were then confirmed to actually carry `CONFIG_SDF_OTA_SIGNATURE_VERIFY=1` before any result below was accepted. All results below are from the rebuilt probe with the fix in place.

```
SIGNED, no --efuse (5.2, 5.3b) — 60s run:
  124 iterations, each: window-captured app_desc/footer -> digest ->
  "Signature verification PASSED" -> esp_ota_end OK -> esp_ota_set_boot_partition OK
  -> "OTA commit successful, rebooting..." -> emulator restart -> next boot repeats
  against the other slot. Zero failures. (An earlier 300s run, before the Kconfig
  fix, reached 748 cycles on the same happy path skeleton but without real signature
  verification compiled in - superseded by this run.)

TAMPERED, no --efuse (5.4, 5.5):
  sdf_ota_sig: Signature verification failed: -0x0095
  sdf_ota: Signature verification failed: ERROR
  sdf_ota_verify_and_commit: ERROR
  redundant sdf_ota_abort: ESP_ERR_INVALID_STATE      <- 5.5
  post-failure sdf_ota_begin: ESP_OK                  <- 5.4, no reboot needed
  PROBE_OTA DONE

UNSIGNED, no --efuse (5.2):
  sdf_ota_sig: Signature magic marker not found (image not signed?)
  sdf_ota: Signature verification failed: ESP_ERR_INVALID_CRC
  sdf_ota_verify_and_commit: ESP_ERR_INVALID_CRC
  PROBE_OTA DONE

RESUME, no --efuse (5.2):
  resume query: ESP_OK device_confirmed=115168 (local sent=115168)
  Signature verification PASSED -> commits and reboots, same as SIGNED

SIGNED, --efuse efuse_enc.bin (5.3a):
  efuse_is_flash_encryption_enabled=1
  Signature verification PASSED                        <- window capture + digest +
                                                            ECDSA check all correct
                                                            under the deferral
  esp_image: Checksum failed. Calculated 0x91 read 0xd7  <- esp_ota_end()'s own
  esp_ota_end failed: ESP_ERR_OTA_VALIDATE_FAILED           internal check, the same
  post-failure sdf_ota_begin: ESP_OK                        emulator artifact as the
  PROBE_OTA DONE                                            task-1 baseline (line 185)
```

This confirms tasks 5.2–5.5: the signed case commits and reboots repeatedly with the real signature check passing every time; tampered and unsigned images are rejected by `sdf_ota_verify_footer()` (not by chance at `esp_ota_end()`); a failed commit — tampered body or the emulator's own encrypted-flash artifact — always leaves the session cleanly closed, so a fresh `sdf_ota_begin()` succeeds immediately and a redundant `sdf_ota_abort()` on the dead handle returns `ESP_ERR_INVALID_STATE` rather than touching anything; and resume queries mid-stream report the true `bytes_written`. Task 5.3a's boundary is exactly where the task-1 baseline predicted it would be — everything this change touches succeeds under the deferral, and the one thing that fails is `esp_ota_end()`'s own internal check hitting the same unfaithful encrypted round-trip documented above, not a regression introduced here.

**Caveat on 5.4's wording.** The task says "force a commit failure on the Zigbee path"; the probe drives `sdf_ota` directly with `SDF_OTA_SOURCE_CLI`, not through `sdf_protocol_zigbee.c`. The un-wedging this task is about — `sdf_ota_begin()` succeeding right after a failure, with no reboot — lives entirely inside `sdf_ota_session_fail()` and does not depend on which transport called in; the run above exercises that code path directly. The Zigbee-specific half of the fix (`s_ota_session = NULL` on the two failure branches in `sdf_protocol_zigbee.c`, task 6.1) is verified by code review only, not by a Zigbee-transport emulator run — no Zigbee coordinator was driven in this session.

**Not run: 5.7, physical hardware.** No device was available in this session. Everything above is emulator-derived; the one configuration no emulator run covers — a real commit under a real, silicon-faithful encrypted round-trip — remains unverified on hardware, exactly as flagged for the task-1 baseline.

## Risks / Trade-offs

- [The version check now runs against a stream-captured `esp_app_desc_t` rather than one read back from flash, so a defect in `sdf_ota_window_capture()` would silently feed the downgrade check the wrong bytes — and the downgrade check is a security control.] → The failure is loud, not silent: a mis-captured window fails the `ESP_APP_DESC_MAGIC_WORD` check and returns `ESP_ERR_NOT_FOUND`, aborting the commit. Backed by exhaustive host tests over chunk splits (task 2.4).
- [+325 bytes of static BSS in `s_session`, permanently resident, on a device with ~512 KB SRAM and no PSRAM.] → 0.06% of SRAM, in `.bss` rather than heap, in exchange for removing an entire class of ordering hazard. Accepted without further analysis.
- [Routing every failure through `sdf_ota_session_fail()` changes observable behavior for callers that previously relied on the session staying `active` after a failure.] → Neither caller does; both treat a failure as terminal (BLE zeroes its session and aborts, Zigbee nulls its handle). The change makes `sdf_ota_begin()` succeed after a failed Zigbee OTA where it previously returned `ESP_ERR_INVALID_STATE` until reboot — a fix, not a regression, but it is a behavior change worth naming.
- [This change is justified by a hazard that is not active in the shipping configuration, so it cannot be validated by observing a bug disappear.] → Validate positively instead: task 5 builds with `CONFIG_SECURE_FLASH_ENC_ENABLED=y` in the emulator and confirms a signed OTA still commits — the run that would fail today. Anything less is untested hardening.
- [Emulator-only validation, as with the P-256 change.] → Same limitation, same mitigation: the emulator runs the real `sdf_ota` component against a real partition table, and the hardware repeat is called out explicitly in tasks rather than assumed.

## Open Questions

- ~~Does `esp-emu` (v0.38.0) model XTS-AES flash encryption faithfully enough for task 5 to mean anything?~~ — **answered, partially.** It models the eFuse gate faithfully, which is all the deferral depends on, so the failure reproduces exactly and needs no stubbing. It does *not* model the encrypted flash data path faithfully, so a commit cannot complete under it. See "Confirmed Outcome: the validation vehicle"; task 5.3 is split accordingly.
