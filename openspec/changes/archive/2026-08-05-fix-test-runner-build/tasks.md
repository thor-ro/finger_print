## 1. Fix Target Pinning

- [x] 1.1 Run `idf.py set-target linux` in `firmware/test_runner` and confirm `sdkconfig` reflects `CONFIG_IDF_TARGET_LINUX=y`
- [x] 1.2 Adjust `firmware/test_runner/sdkconfig.defaults` so a clean checkout building `test_runner` doesn't get guessed as `esp32c6` (decide whether to keep a separate hardware-target defaults file per design.md's Risk note)
- [x] 1.3 Delete stale `firmware/test_runner/build/` and rebuild from scratch: `idf.py build`
- [x] 1.4 Confirm CMake configure succeeds with no "Failed to resolve component" errors (verifies `led_strip`/`esp_adc`/`driver` are no longer pulled in)

## 2. Fix Currently-Wired Tests

- [x] 2.1 Confirm the 9 already-wired test files (enrollment SM, driver utils, driver protocol, Nuki crypto, Nuki pairing, protocol BLE, storage, power, services) build cleanly for `linux`
- [x] 2.2 Run the resulting binary and confirm all currently-wired tests pass; fix any host-target-specific failures that surface
  - All failures that were host-target-specific (missing `sdf_config_init()`, `sdf_services_init()` never called, `CONFIG_SDF_ZIGBEE_ENABLE` defaulting to enabled on a target with no real Zigbee stack) are fixed.
  - Two remaining failures traced to stale test expectations, not the `10u` cap: `fingerprint.h`'s `SDF_FINGERPRINT_USER_ID_MAX` (`10u`) is the intentional, correct max. Confirmed with the user and updated `test_driver_user_id_validation` (now expects `10`, not `0x0FFF`) and `test_enrollment_sm_user_occupied` (now starts enrollment with `user_id=5`, within range, instead of `20`). All 124 wired tests pass.

## 3. Wire In sdf_event_router Tests

- [x] 3.1 Add `sdf_event_router` to `test_runner/main/CMakeLists.txt`'s `EXTRA_COMPONENT_DIRS` and `COMPONENTS`/`REQUIRES`
- [x] 3.2 Add `firmware/components/sdf_event_router/test/test_sdf_event_router.c` to `SRCS`
- [x] 3.3 Build and confirm `test_sdf_event_router_*` tests link and run

## 4. Wire In sdf_protocol_zigbee Tests

- [x] 4.1 Add `sdf_protocol_zigbee` to `EXTRA_COMPONENT_DIRS` and `COMPONENTS`/`REQUIRES` (confirm `zg_priv_reqs` stays empty on `linux` per existing guard)
- [x] 4.2 Add `firmware/components/sdf_protocol_zigbee/test/test_sdf_protocol_zigbee.c` to `SRCS`
- [x] 4.3 Build and confirm `test_sdf_protocol_zigbee_*` tests link and run

## 5. Wire In sdf_cli Tests

- [x] 5.1 Add `sdf_cli` to `EXTRA_COMPONENT_DIRS` and `COMPONENTS`/`REQUIRES`
- [x] 5.2 Add `firmware/components/sdf_cli/test/test_sdf_cli.c` to `SRCS`
- [x] 5.3 Build; if `sdf_cli`'s transitive `REQUIRES` pull in anything hardware-only on `linux` despite the existing guard, resolve or document the gap before proceeding
- [x] 5.4 Confirm `test_sdf_cli_*`, `test_user_*`, `test_nuki_*`, `test_zigbee_*` tests link and run
  - 3 `test_sdf_cli_*` tests PASS; the 11 `test_user_*`/`test_nuki_*`/`test_zigbee_*` tests are pre-existing `TEST_IGNORE_MESSAGE` stubs (e.g. "Requires mock sdf_services") that report IGNORE, not a failure — they link and run as designed, implementing the mocks is out of scope for this change.

## 6. Remove Dangling Lock-Flow References

- [x] 6.1 Delete the `test_lock_flow_*` `extern` declarations from `test_runner_main.c`
- [x] 6.2 Delete the corresponding `RUN_TEST(test_lock_flow_*)` calls
- [x] 6.3 Delete the stray "Assuming these are handled elsewhere..." comment and any other leftover dead commentary in `test_runner_main.c`
- [x] 6.4 Full clean rebuild (`rm -rf build && idf.py build`) and confirm zero warnings about unused/undefined symbols related to lock flow

## 7. Exit Code Wiring

- [x] 7.1 Confirm/adjust `test_runner_main.c`'s `app_main` so the process exit status reflects `UNITY_END()`'s failure count (non-zero on any failure)
- [x] 7.2 Manually verify: introduce a temporary failing assertion, run the binary, confirm non-zero exit; remove the temporary assertion
  - Verified both directions directly: with the two known pre-existing failures present, exit code is `1` (`124 Tests 2 Failures 11 Ignored` / `FAIL`); with those two tests temporarily disabled, exit code is `0` (`122 Tests 0 Failures 11 Ignored` / `OK`). Temporary changes reverted and rebuilt back to the known-good state afterward.

## 8. Documentation

- [x] 8.1 Update `AGENTS.md` Testing section to document the `linux`-target host build/run steps alongside the existing hardware `flash monitor` steps
- [x] 8.2 Update `AGENTS.md` Gotchas to remove or revise "No CI workflows exist yet. Tests require hardware." to reflect that most unit tests now run host-side
- [x] 8.3 Note in `AGENTS.md` (or a follow-up backlog doc) that `sdf_app`/lock-flow tests remain hardware-only pending the open question in design.md
