## MODIFIED Requirements

### Requirement: Wired-In Component Coverage
`test_runner` SHALL compile and execute the Unity test suites for every component whose `REQUIRES`/`PRIV_REQUIRES` resolve cleanly on `IDF_TARGET=linux`: `sdf_state_machines`, `sdf_drivers`, `sdf_protocol_ble`, `sdf_storage`, `sdf_power`, `sdf_services`, `sdf_event_router`, `sdf_protocol_zigbee`, `sdf_cli`, `sdf_config`, `sdf_platform`, `sdf_platform_power`.

#### Scenario: All wired suites run
- **WHEN** the `test_runner` host binary is executed
- **THEN** every `RUN_TEST()` call for the components listed above executes and reports a Unity result (pass/fail), not a skip or crash

#### Scenario: Config and platform coverage included
- **WHEN** the `test_runner` host binary is executed
- **THEN** `sdf_config`'s setter validation, `sdf_platform`'s GPIO/NVS/sleep wrapper behavior, `sdf_platform_power`'s wake/gate wrapper behavior, and `sdf_storage`'s web-user persistence functions each have at least one passing Unity test case
