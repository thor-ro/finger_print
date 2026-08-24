## 1. Enable IDF signed-app verification

- [x] 1.1 Add the signed-app Kconfig block to `firmware/sdkconfig.defaults`: `CONFIG_SECURE_SIGNED_APPS_NO_SECURE_BOOT=y`, `CONFIG_SECURE_SIGNED_APPS_ECDSA_V2_SCHEME=y`, `CONFIG_SECURE_BOOT_ECDSA_KEY_LEN_256_BITS=y`, `CONFIG_SECURE_BOOT_BUILD_SIGNED_BINARIES=y`, `CONFIG_SECURE_BOOT_SIGNING_KEY="ota_signing_key.pem"`. Leave `CONFIG_SECURE_BOOT` unset. (Uses `"../ota_signing_key.pem"` so the path resolves to the already-provisioned repo-root key per task 2.6 — see deviation note in the final report.)
- [x] 1.2 Confirm after `idf.py reconfigure` that the generated `firmware/sdkconfig` contains `CONFIG_SECURE_SIGNED_ON_UPDATE=y` (auto-derived per `Kconfig.projbuild:456-459`) and that `CONFIG_SECURE_BOOT` is still `n`. Verified: `grep` of the generated sdkconfig.
- [x] 1.3 Verify no eFuse-writing step is introduced: `idf.py build` and `idf.py flash` emit no `espefuse` invocation. Verified for `build` (full build log and `build.ninja` both have zero `espefuse` matches); `flash` not exercised (no hardware attached in this environment) but the printed flash command and `flasher_args.json` show only `esptool write-flash`.

## 2. Replace the key pipeline in the build

- [x] 2.1 In `firmware/components/sdf_ota/CMakeLists.txt`, generate `ota_signing_key.pem` (P-256, PEM) at the path `CONFIG_SECURE_BOOT_SIGNING_KEY` names when absent, reusing it when present.
- [x] 2.2 Emit a distinct build message for "generated a new signing key" versus "reusing existing signing key", so a silent regeneration cannot pass unnoticed (spec: `ota-key-autogen` — Regenerated key does not silently break updates).
- [x] 2.3 Delete from the same file: the `extract-pubkey` step, the 65-byte size assertion, the `EMBED_FILES ${OTA_PUBLIC_KEY_BIN}` entry, and the `sign_ota` and `ota_extract_pubkey` custom targets.
- [x] 2.4 Update `.gitignore` to cover `ota_signing_key.pem`, and delete the superseded root artifacts `ota_private.key`, `ota_public.key`, `ota_public_key.bin`, `ota_public_key.bin.pem`. (`.gitignore` was already updated in a prior session; verified with `git check-ignore -v`.)
- [x] 2.5 Delete `tools/sdf_sign_ota.py`.
- [x] 2.6 Generate the project signing key locally and create the `OTA_SIGNING_KEY` GitHub repository secret from its PEM contents. This must exist before task 8.1 lands, or firmware CI fails on the missing secret (D7). (Done in a prior session: `ota_signing_key.pem` exists at repo root and the secret exists on `thor-ro/finger_print`.)

## 3. Strip the custom scheme from sdf_ota

- [x] 3.1 In `firmware/components/sdf_ota/src/sdf_ota_signature.c`, delete the digest accumulator (`sdf_ota_digest_begin/update/finish/release`), the verification core (`sdf_ota_verify_digest`), `sdf_ota_verify_footer`, `sdf_ota_verify_signature`, `sdf_ota_compute_partition_digest`, the embedded-key `extern`, the `SDF_OTA_MAGIC` constant, and the entire `#else` fail-open stub block. Keep `sdf_ota_window_capture()`.
- [x] 3.2 Rename the surviving file to reflect its remaining contents (window capture only) and update `SRCS` in both branches of `CMakeLists.txt`; drop the now-unneeded `mbedtls` from `REQUIRES` if nothing else in the component uses it.
- [x] 3.3 Delete `firmware/components/sdf_ota/include/sdf_ota_digest.h`, relocating the `sdf_ota_window_capture()` declaration and its rationale comment into `sdf_ota.h`.
- [x] 3.4 In `sdf_ota.h`, remove `SDF_OTA_SIG_SIZE`, `SDF_OTA_MAGIC_SIZE`, `SDF_OTA_FOOTER_SIZE`, `SDF_OTA_DIGEST_SIZE`, `SDF_OTA_PUBKEY_SIZE`, and the deleted prototypes. Redefine `SDF_OTA_MIN_IMAGE_SIZE` as `SDF_OTA_APP_DESC_OFFSET + SDF_OTA_APP_DESC_SIZE` (D3). Retain `SDF_ERR_OTA_SIGNATURE_INVALID`.
- [x] 3.5 In `sdf_ota.c`, drop the `digest` and `footer` members from the session struct, `sdf_ota_session_digest_release()` and its call sites, the `sdf_ota_digest_begin()` call in `sdf_ota_begin()`, the `sdf_ota_digest_update()` and footer-window `sdf_ota_window_capture()` calls in `sdf_ota_write()`, and the `digest_finish` + `verify_footer` block in `sdf_ota_verify_and_commit()`. Keep the `app_desc` window capture and the version/downgrade check.
- [x] 3.6 In `sdf_ota_verify_and_commit()`, map `esp_ota_end()`'s `ESP_ERR_OTA_VALIDATE_FAILED` to `SDF_ERR_OTA_SIGNATURE_INVALID`, emit the `OTA_SIGNATURE_INVALID` audit event, and route the failure through the existing `sdf_ota_session_fail()` path so session resources are released (spec: `ota-signature` — Failure is reported as a signature failure; `ota-session-lifecycle` unchanged).
- [x] 3.7 Remove `CONFIG_SDF_OTA_SIGNATURE_VERIFY` from `firmware/components/sdf_config/Kconfig` and from `firmware/sdkconfig.defaults`.

## 4. Remove the CLI verify path

- [x] 4.1 Delete the `ota verify` subcommand body in `firmware/components/sdf_cli/sdf_cli_commands.c`, including its `CONFIG_SDF_OTA_SIGNATURE_VERIFY` guard and the "assume the image fills the partition" size fallback (D6).
- [x] 4.2 Update the `ota` usage string to drop `verify`; leave `version`, `status`, `trigger`, and `rollback` intact.

## 5. Update tests

- [x] 5.1 Delete `firmware/components/sdf_ota/test/test_sdf_ota_signature.c` and `test_sdf_ota_digest.c`, and remove their `extern` declarations and `RUN_TEST()` calls from `firmware/test_runner/main/test_runner_main.c` (lines ~284-288, ~689-693) and their entries from the test `CMakeLists.txt`.
- [x] 5.2 Confirm the window-capture cases in `test_sdf_ota_window.c` still build and pass after the header move, including the every-possible-split case. (Footer-window cases removed since there is no more footer window; app-desc/whole-stream/degenerate-input cases retained.)
- [x] 5.3 Build and run `test_runner` for `IDF_TARGET=linux`; confirm it links with no undefined symbols and exits zero (spec: `firmware-host-test-runner` — No stale references to removed OTA test subjects). Verified: build links with zero errors, `sdf_test_runner.elf` exits 0, Unity summary `302 Tests 0 Failures 11 Ignored`.

## 6. Verify end to end

- [x] 6.1 Build for `esp32c6` and confirm the output artifact carries a Secure Boot V2 signature block; confirm the padded image plus its 4096-byte signature sector fits the OTA partition with headroom. Verified: `espsecure signature-info-v2`/`verify-signature` on `build/sdf.bin` show a valid ECDSA block 0 verifying against the project's own key; unsigned 1,179,648 B → signed 1,183,744 B (+4096 B sector); `check_sizes.py` reports 41% partition headroom.
- [ ] 6.2 Flash the signed image over serial, then transfer a correctly signed image over BLE and confirm it commits and boots (spec: `ota-signature` — Image signed with the running firmware's key accepted). Blocked: no ESP32-C6 hardware attached in this environment.
- [ ] 6.3 Transfer an image tampered with after signing; confirm the commit fails, `OTA_SIGNATURE_INVALID` is emitted, the boot partition is unchanged, and the device stays bootable on the old image. Blocked: no hardware.
- [ ] 6.4 Transfer an image signed with a different key; confirm it is rejected (spec: `ota-signature` — Image signed with a different key rejected). Blocked: no hardware.
- [ ] 6.5 Confirm a session aborted by signature failure leaves the device able to start a subsequent OTA (spec: `ota-session-lifecycle`). Blocked: no hardware.
- [ ] 6.6 Confirm the BLE transport needed no protocol change: the client declares the larger signed-artifact size and chunking/resume behave as before. Blocked: no hardware.
- [ ] 6.7 Confirm the accept and reject cases on real hardware before archiving; `esp-emu` is the CI gate (group 7), hardware is the release check. Blocked: no hardware, and the group 7 CI gate is itself blocked (see below).

## 7. Automate the signature check under esp-emu

- [ ] 7.1 Build the OTA signature test fixture out-of-tree for `esp32c6` following the `AGENTS.md` emulator pattern (`idf.py -B /tmp/... -D SDKCONFIG_DEFAULTS=... set-target esp32c6`, `build`, `merge-bin`), so the in-tree `linux` sdkconfig and `dependencies.lock` stay untouched.  **SUPERSEDED by `add-ble-ota-emulator-harness` (its task 2.1): implemented as `firmware/ota_signature_gate/`, verified building out-of-tree and producing a signed image.**
- [ ] 7.2 Produce three OTA fixtures from that build, all against the same running image: one correctly signed, one signed then byte-flipped in the payload, one re-signed with a throwaway key generated in the job.  **SUPERSEDED by `add-ble-ota-emulator-harness` (its tasks 2.3-2.4, 3.2-3.4a): one signed image plus a foreign signature sector, tampered case derived with repaired checksum/appended hash (D3 there).**
- [ ] 7.3 Drive the three transfers on the emulated device and assert the outcomes: commit succeeds for the first; commit fails with `OTA_SIGNATURE_INVALID` and an unchanged boot partition for the other two. Blocked: the OTA transport is a bonded/encrypted BLE GATT characteristic (`sdf_ble_companion`/`sdf_ble_ota_protocol.c`); driving it against `esp-emu`'s `--ble-hci` bridge requires a host-side BLE GATT central that can pair, bond, and speak that wire format. No such harness exists in the repo, the design does not specify one beyond the high-level intent, and building and validating one from scratch is out of proportion to this implementation session.  **SUPERSEDED by `add-ble-ota-emulator-harness` (Layer 1, its tasks 3.1-3.7, 4.1-4.4): the transport-independent gate drives the three cases through the OTA session API without any BLE transport, which is what D8 actually needed; the BLE-driven variant this task imagined is covered by that change's Layer 2.**
- [ ] 7.4 Run it via `esp-emu --chip esp32c6 --firmware ... --elf ... --timeout ... --exit-on ...` and have the harness exit non-zero on a panic, timeout, or an outcome opposite to the expected one — no retry, no advisory downgrade (spec: `firmware-ci` — Emulator failure fails the check). Blocked: depends on 7.1-7.3.  **SUPERSEDED by `add-ble-ota-emulator-harness` (its tasks 4.1-4.3): `scripts/run_ota_signature_gate.sh` boots the fixture under esp-emu and exits non-zero on failure/panic/timeout.**

## 8. Wire CI

- [ ] 8.1 Add a signing-key step to both firmware-building jobs in `.github/workflows/firmware-ci.yml` that writes `${{ secrets.OTA_SIGNING_KEY }}` to the configured signing key path before `idf.py build`, and fails with a message naming the secret if it is empty. Done for `build-firmware` (the only signed-esp32c6-image job that exists today); the second "firmware-building job" this task anticipates is the `ota-signature (esp32c6 — esp-emu)` job from 8.4, blocked — see task 7.
- [ ] 8.2 Assert the build reused the provisioned key rather than generating one — fail the job on the "generated a new signing key" message from task 2.2 (D7). Done for `build-firmware`; blocked for the 8.4 job for the same reason as 8.1.
- [x] 8.3 Confirm the key never reaches logs or artifacts: no `cat`/`echo` of the file, and no upload step covering the project root key path. Verified by review of `firmware-ci.yml`: the key-write step only tests for emptiness, never echoes content, and no `upload-artifact` step exists anywhere in the file.
- [ ] 8.4 Add a third job `ota-signature (esp32c6 — esp-emu)` running the task 7 harness in the `espressif/idf:v6.0.2` container with `esp-emu` installed, independent of `build-firmware` and `test-firmware` (spec: `firmware-ci` — Independent Parallel Jobs). Blocked: depends on the task 7 harness, which is blocked (see task 7).  **SUPERSEDED by `add-ble-ota-emulator-harness` (its tasks 5.1-5.4): the job exists in `.github/workflows/firmware-ci.yml` running Layer 1.**
- [ ] 8.5 Verify on a branch push that all three jobs run in parallel and that a deliberately broken signature fixture turns the new check red. Blocked: depends on 8.4, and a branch push is outside this session's scope (no push/commit without explicit request).  **Superseded by `add-ble-ota-emulator-harness` (its task 5.5, equally pending a branch push to verify on GitHub Actions itself).**

## 9. Documentation

- [x] 9.1 Replace `idf.py sign_ota` references with the ESP-IDF signing procedure in `version.md`, `README.md`, and `AGENTS.md` wherever they appear. (`README.md` had no such references to begin with — verified by grep.)
- [x] 9.2 Document the provisioning requirement — the first image must be flashed signed over serial — and the rollback constraint that crossing the format boundary in either direction requires a serial reflash.
- [x] 9.3 Document that signing-key rotation requires a serial reflash and cannot be staged over the air (D1, fact 3).
