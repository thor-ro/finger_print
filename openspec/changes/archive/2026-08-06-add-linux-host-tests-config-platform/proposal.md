## Why

`firmware/components/{sdf_config,sdf_platform,sdf_platform_power}/test/` are empty. This isn't a build blocker like the other two test gaps in this batch: all three components are already listed in `test_runner`'s `EXTRA_COMPONENT_DIRS`/`COMPONENTS` (`firmware/test_runner/CMakeLists.txt`) and already build cleanly for `IDF_TARGET=linux` — `sdf_platform` has an explicit `linux` branch in its `CMakeLists.txt`, and `sdf_config`/`sdf_platform_power` have no hardware-only `REQUIRES` at all. Nobody has written the Unity suites, that's the entire gap.

Separately, `sdf_storage_web_user_{save,load,find_by_name,clear,clear_all,count}()` (`firmware/components/sdf_storage/src/sdf_storage.c`) — the persistence layer backing web companion login/registration — has zero test cases in the existing (non-empty, already-wired) `test_sdf_storage.c`, even though every other storage-key family in that file is covered.

Recent commits shipping bugs referred to as "A4/A7" (per `add-firmware-ci`'s proposal) motivate closing cheap, already-buildable coverage gaps before that CI change lands, so it starts gating on a fuller suite rather than the current subset.

## What Changes

- Add `firmware/components/sdf_config/test/test_sdf_config.c` covering config getter/setter validation (the same setters `sdf_ble_companion_apply_config_u32()` in `sdf_app.c` calls: bounds/rejection behavior for `checkin_interval_ms`, `failed_attempt_threshold`, `battery_default_percent`, etc.).
- Add `firmware/components/sdf_platform/test/test_sdf_platform.c` covering the platform HAL wrappers already linux-mocked (GPIO, sleep, NVS wrappers) via `sdf_mock_linux_gpio.c`/etc. in `sdf_common`.
- Add `firmware/components/sdf_platform_power/test/test_sdf_platform_power.c` covering its power-state wrapper behavior.
- Add web-user coverage to the existing `firmware/components/sdf_storage/test/test_sdf_storage.c`: save/load round-trip, load-not-found, find-by-name (hit/miss), clear/clear_all, count, and the max-slot boundary (`SDF_STORAGE_WEB_USER_MAX`).
- Wire all new/extended suites into `firmware/test_runner/main/CMakeLists.txt` (`SRCS`) and `firmware/test_runner/main/test_runner_main.c` (`extern`/`RUN_TEST()`).
- Extend the `firmware-host-test-runner` spec's "Wired-In Component Coverage" requirement to list `sdf_config`, `sdf_platform`, `sdf_platform_power` alongside the components already named there.

## Capabilities

### New Capabilities
(none)

### Modified Capabilities
- `firmware-host-test-runner`: "Wired-In Component Coverage" requirement extends to `sdf_config`, `sdf_platform`, `sdf_platform_power` (already build-graph members, previously excluded from the requirement only because they had no `RUN_TEST()`s to execute).

## Impact

- New/extended test files under `firmware/components/{sdf_config,sdf_platform,sdf_platform_power,sdf_storage}/test/`.
- `firmware/test_runner/main/CMakeLists.txt`, `firmware/test_runner/main/test_runner_main.c`.
- No production firmware behavior changes.
- Independent of `spike-sdf-ota-linux-target` and `add-linux-target-sdf-app-support` — can land first, in parallel, or last; no ordering dependency between the three.
