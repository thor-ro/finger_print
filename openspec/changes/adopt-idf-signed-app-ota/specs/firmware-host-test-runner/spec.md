## ADDED Requirements

### Requirement: Host-Testable Component Coverage
`test_runner` SHALL compile and execute the Unity test suites for every component whose `REQUIRES`/`PRIV_REQUIRES` resolve cleanly on `IDF_TARGET=linux`: `sdf_state_machines`, `sdf_drivers`, `sdf_protocol_ble`, `sdf_storage`, `sdf_power`, `sdf_services`, `sdf_event_router`, `sdf_protocol_zigbee`, `sdf_cli`, `sdf_config`, `sdf_platform`, `sdf_platform_power`, `sdf_ota`.

#### Scenario: All wired suites run
- **WHEN** the `test_runner` host binary is executed
- **THEN** every `RUN_TEST()` call for the components listed above executes and reports a Unity result (pass/fail), not a skip or crash

#### Scenario: Config and platform coverage included
- **WHEN** the `test_runner` host binary is executed
- **THEN** `sdf_config`'s setter validation, `sdf_platform`'s GPIO/NVS/sleep wrapper behavior, `sdf_platform_power`'s wake/gate wrapper behavior, and `sdf_storage`'s web-user persistence functions each have at least one passing Unity test case

#### Scenario: sdf_ota linux-safe subset covered
- **WHEN** the `test_runner` host binary is executed
- **THEN** `sdf_ota_version.c`'s semver parsing/comparison and the transfer-window capture primitive each have at least one passing Unity test case
- **AND** `sdf_ota.c`'s partition-write/rollback logic is not part of this coverage (excluded pending a `linux` stub for its `sdf_app_emit_audit` dependency)
- **AND** OTA signature verification is not part of this coverage, because it is performed by ESP-IDF's `bootloader_support`, which does not build for `IDF_TARGET=linux`

#### Scenario: No stale references to removed OTA test subjects
- **WHEN** the `test_runner` host binary is built
- **THEN** it declares no `extern`/`RUN_TEST()` entries for the removed streaming-digest or signature-verification-core test cases

### Requirement: On-Target Coverage for Signature Rejection
Because OTA signature verification cannot be exercised on `IDF_TARGET=linux`, the project SHALL cover it with an automated test that runs on the `esp32c6` chip target under `esp-emu`, asserting that a tampered or wrongly-signed image is rejected at commit and a correctly signed one is accepted. This coverage SHALL NOT be left as a manual hardware step.

#### Scenario: Tampered image rejected on target
- **WHEN** an image whose bytes have been modified after signing is transferred to an emulated device
- **THEN** the commit fails, the image does not become the boot partition, and an `OTA_SIGNATURE_INVALID` audit event is emitted

#### Scenario: Correctly signed image accepted on target
- **WHEN** an image signed with the running firmware's key is transferred to an emulated device
- **THEN** the commit succeeds and the image becomes the boot partition

#### Scenario: Emulator run is a gate
- **WHEN** the emulator run panics, times out, or reports an outcome opposite to the expected one
- **THEN** the test reports failure rather than being skipped, retried, or downgraded to advisory

## REMOVED Requirements

### Requirement: Wired-In Component Coverage
**Reason**: Two of its scenarios name test subjects that this change deletes — `sdf_ota_signature.c`'s ECDSA P-256 digest verification core and the streaming digest accumulator. Verification moves into ESP-IDF's `bootloader_support`, which does not build for `IDF_TARGET=linux`, so neither can be covered on the host. Replaced by "Host-Testable Component Coverage", which keeps the component list and the surviving `sdf_ota` coverage, plus "On-Target Coverage for Signature Rejection" for what moves off the host.

**Migration**: Delete `test_sdf_ota_signature.c` and `test_sdf_ota_digest.c` along with their `RUN_TEST()` wiring; retain the window-capture and semver cases.
