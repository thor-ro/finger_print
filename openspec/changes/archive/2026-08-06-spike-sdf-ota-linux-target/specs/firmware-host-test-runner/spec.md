## MODIFIED Requirements

### Requirement: Wired-In Component Coverage
`test_runner` SHALL compile and execute the Unity test suites for every component whose `REQUIRES`/`PRIV_REQUIRES` resolve cleanly on `IDF_TARGET=linux`: `sdf_state_machines`, `sdf_drivers`, `sdf_protocol_ble`, `sdf_storage`, `sdf_power`, `sdf_services`, `sdf_event_router`, `sdf_protocol_zigbee`, `sdf_cli`, `sdf_ota`.

#### Scenario: All wired suites run
- **WHEN** the `test_runner` host binary is executed
- **THEN** every `RUN_TEST()` call for the components listed above executes and reports a Unity result (pass/fail), not a skip or crash

#### Scenario: sdf_ota linux-safe subset covered
- **WHEN** the `test_runner` host binary is executed
- **THEN** `sdf_ota_version.c`'s semver parsing/comparison and `sdf_ota_signature.c`'s default-configuration (`CONFIG_SDF_OTA_SIGNATURE_VERIFY=n`) no-op verification path each have at least one passing Unity test case
- **AND** `sdf_ota.c`'s partition-write/rollback logic is not part of this coverage (excluded pending a `linux` stub for its `sdf_app_emit_audit` dependency)

**Note (spike outcome):** This delta only applies if `spike-sdf-ota-linux-target`'s `idf.py build` attempt for `IDF_TARGET=linux` succeeds. If it doesn't, this spec delta is dropped from the change before archiving, per that change's own proposal and design.md.
