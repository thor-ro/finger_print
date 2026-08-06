## 1. Build Attempt

- [x] 1.1 Add `sdf_ota` to `firmware/test_runner/CMakeLists.txt`'s `EXTRA_COMPONENT_DIRS` and `COMPONENTS`
- [x] 1.2 Split `firmware/components/sdf_ota/CMakeLists.txt` with an `if(IDF_TARGET STREQUAL "linux")`/`else()` branch: linux branch registers only `sdf_ota_version.c` + `sdf_ota_signature.c` with `REQUIRES mbedtls sdf_common` (no `app_update`, no `PRIV_REQUIRES sdf_config`, no `EMBED_FILES`); else branch keeps the existing full registration unchanged
- [x] 1.3 Confirm the `openssl`/`sdf_sign_ota.py` key-generation `execute_process()` block at the top of the file still needs to run unconditionally for a linux build, or guard it too — record the finding either way
- [x] 1.4 Run `cd firmware/test_runner && idf.py set-target linux && idf.py build`; record the exact result (clean build, or the first failing symbol/component)
- [x] 1.5 Run `cd firmware && idf.py build` (hardware/`esp32c6` target) to confirm the `CMakeLists.txt` split didn't change the hardware build's compiled output

## 2a. If the build succeeds

- [x] 2a.1 Add `firmware/components/sdf_ota/test/test_sdf_ota_version.c`: valid semver parse (with/without leading `v`), pre-release suffix handling, malformed input rejection, and `sdf_ota_version_compare()`'s ordering across major/minor/patch/pre-release
- [x] 2a.2 Add `firmware/components/sdf_ota/test/test_sdf_ota_signature.c`: `sdf_ota_verify_signature()` under the default `CONFIG_SDF_OTA_SIGNATURE_VERIFY=n` build returns `ESP_OK` regardless of input (pins the current no-op contract)
- [x] 2a.3 Wire both files into `firmware/test_runner/main/CMakeLists.txt`'s `SRCS` and add `extern`/`RUN_TEST()` entries to `firmware/test_runner/main/test_runner_main.c`
- [x] 2a.4 Run `./build/sdf_test_runner.elf` from `firmware/test_runner`, confirm all new and existing tests pass
- [x] 2a.5 Keep this change's `specs/firmware-host-test-runner/spec.md` delta as-is
- [x] 2a.6 Note in this change's design.md (Open Questions) or a follow-up issue: whether `sdf_ota_signature.c`'s `CONFIG_SDF_OTA_SIGNATURE_VERIFY=y` branch's `#include "sdf_app.h"` without a corresponding `REQUIRES sdf_app` is a pre-existing hardware-build gap, confirmed or not, from step 1.4/1.5's output

## 2b. If the build fails

- [ ] 2b.1 Record the exact failing symbol/component/error in design.md's Risks or Migration Plan section (update the existing draft with the real outcome)
- [ ] 2b.2 Revert `firmware/test_runner/CMakeLists.txt`'s `sdf_ota` addition
- [ ] 2b.3 Keep the `CMakeLists.txt` `IDF_TARGET` split regardless (still correct/self-documenting for the hardware-vs-linux boundary even if the linux side doesn't fully build yet)
- [ ] 2b.4 Delete `specs/firmware-host-test-runner/spec.md` from this change before archiving — do not claim `sdf_ota` coverage that doesn't exist
- [ ] 2b.5 Update `proposal.md`'s Capabilities section to reflect "no spec change, findings captured in design.md" as the actual outcome

## 3. Validation

- [x] 3.1 Run `openspec validate spike-sdf-ota-linux-target --strict`
