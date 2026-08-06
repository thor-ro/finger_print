## Context

`firmware/test_runner` is an ESP-IDF sub-project that links a subset of SDF components plus Unity and runs their `test/*.c` suites. `AGENTS.md` documents it as a hardware-flashed test app, but the component tree has already been partially prepared for host-side testing: several components (`sdf_platform`, `sdf_drivers`, `sdf_power`, `sdf_protocol_ble`, `sdf_services`, `sdf_protocol_zigbee`) carry `if(NOT IDF_TARGET STREQUAL "linux")` guards around hardware-only `REQUIRES`, and `*_mock_linux.c` source files exist for `sdf_drivers` and `sdf_protocol_zigbee`. Nobody finished wiring this up, and nobody has been running it — `test_runner/build/log/` shows two failed `idf.py` configure attempts today, both because CMake guessed the `esp32c6` target (from `test_runner/sdkconfig.defaults`) instead of `linux`, pulling in `led_strip`, which was never fetched into `test_runner/managed_components`.

Independently, `test_runner/main/test_runner_main.c` declares `extern`/`RUN_TEST()` calls for `sdf_cli`, `sdf_event_router`, `sdf_protocol_zigbee`, and `sdf_app` (lock-flow) tests that were never added to `test_runner/main/CMakeLists.txt`'s `SRCS`. A stray comment in the file suggests a previous edit attempt was abandoned mid-way. This would fail to link regardless of target.

## Goals / Non-Goals

**Goals:**
- `test_runner` builds and links successfully for `IDF_TARGET=linux` with zero dangling symbol references.
- Every test file that *can* build against `linux`-clean components (i.e. components with no unresolved hardware-only `REQUIRES` on that target) is wired into `SRCS` and executed by `test_runner_main.c`.
- The resulting host binary exits non-zero on any Unity test failure, so it's directly usable as a CI gate (`add-firmware-ci` depends on this).
- `AGENTS.md` reflects the new host-testing path.

**Non-Goals:**
- Making `sdf_app`/lock-flow tests runnable on `linux` in this change. `sdf_app` unconditionally requires `sdf_ble_companion`, which unconditionally requires `bt`/`esp_wifi`/`esp_netif`/`esp_http_client` — none available on `IDF_TARGET=linux`. Making it host-testable means either conditionally compiling companion calls out of `sdf_app.c` on `linux` or extracting lock-flow logic so it doesn't pull the whole component in — both are real architectural changes to production code, not a build fix, and carry regression risk out of proportion to this change's scope.
- Changing the on-hardware `flash monitor` test path — it remains available for whichever tests still need real peripherals (fingerprint sensor, LEDs) or full integration verification.
- Standing up CI itself (that's `add-firmware-ci`, which depends on this change).

## Decisions

**Pin `test_runner` to `linux` explicitly, don't rely on target inference.**
Add `set(IDF_TARGET linux)`-equivalent pinning — concretely, run `idf.py set-target linux` as documented setup, and align `test_runner/sdkconfig.defaults` so a bare `idf.py build` from a clean `sdkconfig` doesn't silently re-guess `esp32c6`. This directly removes the `led_strip` failure: `sdf_drivers`' guard already excludes `led_strip`/`esp_adc`/`driver` for `linux`, so once the target is correct, that dependency is never requested. Alternative considered: fetch `led_strip` into `test_runner/managed_components` to satisfy the `esp32c6` guess — rejected, because that's fixing a symptom (wrong target still gets guessed) rather than the cause, and would leave `test_runner` compiling driver/ADC code nobody intends to exercise host-side.

**Wire in `sdf_event_router` and `sdf_protocol_zigbee` test suites now.**
Both components are already fully `linux`-clean (`sdf_event_router` has no hardware `REQUIRES` at all; `sdf_protocol_zigbee` guards `esp-zigbee-lib`/`app_update`/`spi_flash`/`sdf_ota` behind the same `NOT IDF_TARGET STREQUAL "linux"` check and ships a `_mock_linux.c`). Adding their component dirs to `EXTRA_COMPONENT_DIRS`, `sdf_event_router sdf_protocol_zigbee` to `REQUIRES`, and their `test/*.c` to `SRCS` is a pure additive wiring change with no guard changes needed.

**Wire in `sdf_cli` test suite now.**
`sdf_cli`'s own `PRIV_REQUIRES` are `linux`-safe: `console esp_timer log nvs_flash sdf_services sdf_storage sdf_drivers sdf_protocol_zigbee sdf_platform sdf_ota`, with `sdf_app sdf_protocol_ble esp-zigbee-lib` appended only `if(NOT IDF_TARGET STREQUAL "linux")`. So `sdf_cli` itself already avoids the `sdf_app`/companion dependency chain on `linux`. Add it to `test_runner`'s `EXTRA_COMPONENT_DIRS`/`REQUIRES` and wire `test/test_sdf_cli.c` into `SRCS`.

**Remove dangling `test_lock_flow_*` references, don't half-fix them.**
Rather than leave broken `extern`/`RUN_TEST()` calls in `test_runner_main.c` (today's state) or attempt a partial `sdf_app`/`sdf_ble_companion` linux-guard change under time pressure, delete the lock-flow `extern` declarations and `RUN_TEST()` calls from `test_runner_main.c` in this change. This restores a clean, fully-linking build immediately. Extending `linux` support down through `sdf_app`/`sdf_ble_companion` is tracked as an Open Question / follow-up, not bundled here.

**Fail loud, not silent, on future drift.**
`test_runner_main.c` should end with a call that returns Unity's failure count as process exit code (`return UNITY_END();` from `app_main`, or equivalent for the `linux` target's `app_main` wrapper), so `add-firmware-ci` can rely on the process exit code rather than scraping stdout for "FAIL".

## Risks / Trade-offs

- [Wiring in `sdf_cli`/`sdf_event_router`/`sdf_protocol_zigbee` tests for the first time on `linux` may surface latent host-build issues those tests never hit before (they've only ever been hardware-compiled, if at all)] → Build and run locally before considering the change done; fix forward rather than reverting to broken state.
- [Removing `test_lock_flow_*` wiring leaves lock-flow logic without host-side test coverage indefinitely] → Explicitly tracked as an open question below with a proposed follow-up change name, so it doesn't silently disappear from the backlog.
- [`test_runner/sdkconfig.defaults` currently mirrors the hardware target's defaults; retargeting to `linux` may drop settings the hardware test path still needs for `flash monitor` runs] → Keep hardware-target sdkconfig as a separate defaults file (e.g. `sdkconfig.hw.defaults`) if `AGENTS.md`'s documented `flash monitor` flow is still meant to work; verify during implementation rather than assuming.

## Migration Plan

1. Fix `test_runner` target pinning; confirm the currently-wired 9 test files (enrollment SM, driver utils/protocol, Nuki crypto/pairing, protocol BLE, storage, power, services) build and pass on `linux`.
2. Wire in `sdf_event_router`, `sdf_protocol_zigbee`, `sdf_cli` tests; confirm they build and pass.
3. Remove dangling `test_lock_flow_*` references.
4. Update `AGENTS.md`.
5. No production firmware or runtime behavior changes — nothing to roll back beyond reverting the commit if `test_runner` regresses.

## Open Questions

- Should `sdf_app`/lock-flow become `linux`-testable by adding a guard through `sdf_ble_companion` (and conditionally compiling companion calls out of `sdf_app.c`)? If yes, worth its own change (e.g. `add-linux-target-sdf-app-support`) once this one lands.
- Does the hardware `flash monitor` test path (per `AGENTS.md`) still need to work as-is, or is `linux`-host testing meant to fully replace it? Affects whether `test_runner` needs two sdkconfig profiles or just one.
