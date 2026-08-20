# Firmware Host Test Runner

## Purpose

Defines host-side test runner requirements for compiling and executing Unity test suites on `IDF_TARGET=linux`.

## Requirements

### Requirement: Host-Target Build
`test_runner` SHALL build for `IDF_TARGET=linux` without depending on any hardware-only component (`driver`, `esp_adc`, `led_strip`, `bt`, `esp_wifi`, `esp_netif`, `esp_http_client`, `espressif__esp-zigbee-lib`).

#### Scenario: Clean configure
- **WHEN** `cd firmware/test_runner && idf.py set-target linux && idf.py build` is run from a clean `build/` directory
- **THEN** CMake configure succeeds with no "Failed to resolve component" errors
- **THEN** the build completes and produces a host-executable binary

### Requirement: No Dangling Test References
`test_runner/main/test_runner_main.c` SHALL only declare `extern`/`RUN_TEST()` calls for test functions that are compiled into the `test_runner` binary via `test_runner/main/CMakeLists.txt`'s `SRCS`.

#### Scenario: Link succeeds
- **WHEN** `test_runner` is built for `IDF_TARGET=linux`
- **THEN** linking succeeds with no undefined-symbol errors for any `extern`-declared test function

### Requirement: Wired-In Component Coverage
`test_runner` SHALL compile and execute the Unity test suites for every component whose `REQUIRES`/`PRIV_REQUIRES` resolve cleanly on `IDF_TARGET=linux`: `sdf_state_machines`, `sdf_drivers`, `sdf_protocol_ble`, `sdf_storage`, `sdf_power`, `sdf_services`, `sdf_event_router`, `sdf_protocol_zigbee`, `sdf_cli`, `sdf_config`, `sdf_platform`, `sdf_platform_power`, `sdf_ota`.

#### Scenario: All wired suites run
- **WHEN** the `test_runner` host binary is executed
- **THEN** every `RUN_TEST()` call for the components listed above executes and reports a Unity result (pass/fail), not a skip or crash

#### Scenario: Config and platform coverage included
- **WHEN** the `test_runner` host binary is executed
- **THEN** `sdf_config`'s setter validation, `sdf_platform`'s GPIO/NVS/sleep wrapper behavior, `sdf_platform_power`'s wake/gate wrapper behavior, and `sdf_storage`'s web-user persistence functions each have at least one passing Unity test case

#### Scenario: sdf_ota linux-safe subset covered
- **WHEN** the `test_runner` host binary is executed
- **THEN** `sdf_ota_version.c`'s semver parsing/comparison and `sdf_ota_signature.c`'s ECDSA P-256 digest verification core each have at least one passing Unity test case
- **AND** the signature test cases exercise real cryptographic verification against known-answer vectors, not a disabled no-op path
- **AND** `sdf_ota.c`'s partition-write/rollback logic is not part of this coverage (excluded pending a `linux` stub for its `sdf_app_emit_audit` dependency)

#### Scenario: Streaming digest accumulation covered
- **WHEN** the `test_runner` host binary is executed
- **THEN** at least one passing Unity test case asserts that a digest accumulated across a sequence of variable-sized chunks equals the digest of the equivalent contiguous byte range

### Requirement: Process Exit Code Reflects Test Result
The `test_runner` host binary SHALL exit with a non-zero status if any Unity test fails, and zero if all tests pass.

#### Scenario: All tests pass
- **WHEN** every wired-in test passes
- **THEN** the process exits with status `0`

#### Scenario: A test fails
- **WHEN** at least one wired-in test fails
- **THEN** the process exits with a non-zero status
