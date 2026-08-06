## Why

`firmware/components/sdf_ota/test/` is empty, and unlike `sdf_ota`'s siblings, it's genuinely unknown whether that's fixable the cheap way. Every other component with hardware-only dependencies in this codebase (`sdf_platform`, `sdf_drivers`, `sdf_power`, `sdf_protocol_ble`, `sdf_services`, `sdf_protocol_zigbee`) guards them behind `if(NOT IDF_TARGET STREQUAL "linux")` in its `CMakeLists.txt`, and several ship `*_mock_linux.c` files so the linux build has something to link against. `sdf_ota`'s `CMakeLists.txt` has no such guard — `REQUIRES mbedtls app_update sdf_common` is unconditional — and the component isn't in `test_runner`'s `EXTRA_COMPONENT_DIRS`/`COMPONENTS` at all, meaning nobody has ever attempted to build it for `IDF_TARGET=linux`. `app_update` is an ESP-IDF component built around real flash OTA partitions; whether it (or `esp_partition`, which it depends on and which `test_runner` already links for other components) builds and behaves sanely under the `linux` target's simulated partition support is an open question, not a known blocker.

This change is a spike: answer that question with evidence, then either follow through with a Tier-1-style test-writing change or hand off a scoped-and-documented blocker to whoever picks up hardware-only mocking work next.

## What Changes

- Attempt adding `sdf_ota` to `firmware/test_runner/CMakeLists.txt`'s `EXTRA_COMPONENT_DIRS`/`COMPONENTS` and run `idf.py set-target linux && idf.py build` from `firmware/test_runner/`.
- **If it builds cleanly**: wire in a first-pass Unity suite for the linux-safe subset — `sdf_ota_version.c` (version compare/parse) and `sdf_ota_signature.c` (Ed25519 signature verification, which only needs `mbedtls` — already linux-tested elsewhere in this repo) are good candidates since neither touches a real flash partition. Leave `sdf_ota.c`'s actual partition-write/rollback logic for a follow-up if it needs a `linux`-specific mock (mirroring `sdf_drivers`'/`sdf_protocol_zigbee`'s `*_mock_linux.c` pattern) — don't block this change on that.
- **If it doesn't build**: capture the exact failing symbol/component in this change's `design.md` (or a short note) rather than reverting silently, so a future "mock `app_update` for linux" effort has a concrete starting point instead of re-discovering the same wall.
- Either outcome: `sdf_ota`'s `CMakeLists.txt` gets an explicit guard (`if(NOT IDF_TARGET STREQUAL "linux") ... endif()` around whatever turns out to be hardware-only) so the linux-buildability status is self-documenting going forward, matching every other component's convention.
- Update the `firmware-host-test-runner` spec's "Wired-In Component Coverage" requirement to add `sdf_ota` — only if the build succeeds.

## Capabilities

### New Capabilities
(none)

### Modified Capabilities
- `firmware-host-test-runner`: "Wired-In Component Coverage" requirement conditionally extends to `sdf_ota`, contingent on this spike's outcome. If the build doesn't succeed, this section is dropped from the change before it's applied and the finding is tracked as a follow-up instead.

## Impact

- `firmware/components/sdf_ota/CMakeLists.txt` (explicit linux guard).
- `firmware/test_runner/CMakeLists.txt` (`EXTRA_COMPONENT_DIRS`/`COMPONENTS`), possibly reverted if the spike is negative.
- Possible new `firmware/components/sdf_ota/test/test_sdf_ota_version.c` / `test_sdf_ota_signature.c`.
- No production firmware behavior changes either way.
- Independent of `add-linux-host-tests-config-platform` and `add-linux-target-sdf-app-support` — no ordering dependency.
