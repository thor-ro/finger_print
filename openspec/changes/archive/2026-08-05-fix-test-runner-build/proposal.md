## Why

`firmware/test_runner` — the Unity test harness that links SDF components and runs the unit test suite — currently fails to configure at all, and even if configuration were fixed, would fail to link. Nobody has caught this because there is no CI and, per `AGENTS.md`, tests are documented as requiring hardware, so nobody runs `test_runner` routinely on a host. Two independent problems compound:

1. **Target mis-detection.** `test_runner/sdkconfig.defaults` targets `esp32c6`. Nobody runs `idf.py set-target linux` before building, so CMake guesses `esp32c6`, which pulls in `driver`/`esp_adc`/`led_strip` (only excluded when `IDF_TARGET STREQUAL "linux"`). `led_strip` was never fetched into `test_runner/managed_components` (only the main `firmware/` tree has it), so CMake configure fails immediately. Confirmed via two failed `idf.py` runs logged in `test_runner/build/log/` today, both dying at the identical `led_strip` resolution error.
2. **Orphaned test wiring.** `test_runner_main.c` declares `extern`/`RUN_TEST()` calls for `sdf_cli`, `sdf_event_router`, `sdf_protocol_zigbee`, and `sdf_app` (lock flow) tests that were never added to `test_runner/main/CMakeLists.txt`'s `SRCS`. A comment left in the file ("Assuming these are handled elsewhere, or were incorrectly added in my previous step, removing invalid macro") indicates a prior edit was abandoned mid-fix. This would fail to link even with the target problem fixed.

This is exactly the kind of regression `add-firmware-ci` needs a working target to catch — CI is pointless pointed at a build that's already broken. Fix the test runner first, cleanly, before wiring CI to it.

## What Changes

- Pin `test_runner` to `IDF_TARGET=linux` explicitly (own `sdkconfig.defaults` target line, not inferred), so `led_strip`/`esp_adc`/`driver` are never pulled in and the host build no longer depends on `managed_components` for hardware-only libs.
- Wire `sdf_event_router` and `sdf_protocol_zigbee` test suites into `test_runner` (both components are already `linux`-clean — no guard changes needed): add their component dirs to `EXTRA_COMPONENT_DIRS`/`REQUIRES` and their `test/*.c` files to `SRCS`.
- Wire `sdf_cli` test suite into `test_runner`: `sdf_cli`'s own `REQUIRES` are already `linux`-safe (it only pulls `sdf_app`/`sdf_protocol_ble`/`esp-zigbee-lib` when `NOT IDF_TARGET STREQUAL "linux"`), so this is an additive wiring change, not a guard change.
- Resolve `sdf_app`/`test_lock_flow.c`: `sdf_app` unconditionally requires `sdf_ble_companion`, which unconditionally requires `bt`/`esp_wifi`/`esp_netif`/`esp_http_client` — none of which build for `IDF_TARGET=linux`. Either add a `linux`-target guard through `sdf_ble_companion` (and confirm `sdf_app.c` compiles with companion calls stubbed/excluded) so lock-flow tests can run host-side, or explicitly remove the dangling `test_lock_flow_*` references from `test_runner_main.c` and track host-testability for `sdf_app` as separate follow-up work. Decision captured in design.md.
- Remove all dangling `extern`/`RUN_TEST()` declarations that don't correspond to a compiled/linked test file, so `test_runner` always links cleanly.
- Update `AGENTS.md` Testing section and Gotchas ("Tests require hardware") to reflect that most unit tests now also run host-side via `IDF_TARGET=linux`, while the on-hardware `flash monitor` path remains for the tests that stay hardware-only (or for full integration/HIL verification).

## Capabilities

### New Capabilities
- `firmware-host-test-runner`: `test_runner` builds and links cleanly for `IDF_TARGET=linux` with no dangling test references, runs every wired-in component's Unity suite host-side, and exits non-zero on any test failure (suitable for CI consumption).

### Modified Capabilities
(none — no existing active spec covers the test runner; the only prior mention lived in an archived `build-test-config` spec that described hardware-only testing)

## Impact

- `firmware/test_runner/sdkconfig.defaults`, `firmware/test_runner/main/CMakeLists.txt`, `firmware/test_runner/main/test_runner_main.c`
- Possibly `firmware/components/sdf_ble_companion/CMakeLists.txt` and `firmware/components/sdf_app/src/sdf_app.c` (if the lock-flow linux path is pursued) — scope decided in design.md
- `AGENTS.md` (Testing section, Gotchas)
- No production firmware behavior changes; build/test tooling only.
