## MODIFIED Requirements

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
