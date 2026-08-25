# Spec: firmware-ci

## Purpose

Defines the CI workflow that gates `firmware/**` pushes and pull requests: what triggers it, which pinned toolchain it builds with, what its build and unit-test checks must verify, and how its jobs relate to each other.

## Requirements

### Requirement: Trigger Scope
The firmware CI workflow SHALL run on `push` and `pull_request` events that touch `firmware/**` or the workflow file itself.

#### Scenario: Firmware change triggers workflow
- **WHEN** a push or pull request modifies any file under `firmware/`
- **THEN** the firmware CI workflow runs

#### Scenario: Unrelated change does not trigger workflow
- **WHEN** a push or pull request only modifies files outside `firmware/` (e.g. `web-companion/`, `doc/`)
- **THEN** the firmware CI workflow does not run

### Requirement: Pinned Toolchain
The firmware CI workflow SHALL build using ESP-IDF v6.0.2, matching the version documented in `AGENTS.md`.

#### Scenario: Build environment version
- **WHEN** any CI job in the workflow runs `idf.py`
- **THEN** it runs inside an environment providing ESP-IDF v6.0.2

### Requirement: Firmware Build Check
The workflow SHALL build the main firmware for the `esp32c6` target and fail the check on any compile error.

#### Scenario: Successful compile
- **WHEN** `idf.py build` succeeds in `firmware/`
- **THEN** the build job reports success

#### Scenario: Compile error
- **WHEN** `idf.py build` fails in `firmware/` (e.g. syntax error, missing symbol, unresolved component)
- **THEN** the build job reports failure and the CI check is marked failed

### Requirement: Host Unit Test Check
The workflow SHALL build `firmware/test_runner` for `IDF_TARGET=linux` and execute the resulting binary, failing the check if the process exits non-zero.

#### Scenario: All tests pass
- **WHEN** the `test_runner` host binary exits with status `0`
- **THEN** the unit test job reports success

#### Scenario: A test fails
- **WHEN** the `test_runner` host binary exits with a non-zero status
- **THEN** the unit test job reports failure and the CI check is marked failed

### Requirement: Signing Key Provisioning
The firmware CI workflow SHALL materialize the project's OTA image signing key from a repository secret before any job that builds firmware, writing it to the path the build's signing configuration names. The key SHALL be the same key developers use locally, so that CI-built firmware can update a locally provisioned device and vice versa.

#### Scenario: Key materialized from the repository secret
- **WHEN** a firmware-building job starts
- **THEN** the signing key is written from the repository secret to the configured signing key path before `idf.py build` runs
- **AND** the resulting build artifact carries a signature block signed by that key

#### Scenario: Missing secret fails the check
- **WHEN** the signing key secret is absent or empty
- **THEN** the job fails rather than proceeding with a build-generated throwaway key
- **AND** the failure names the missing secret

#### Scenario: Key is not exposed in logs or artifacts
- **WHEN** a firmware-building job completes
- **THEN** the signing key's contents do not appear in job logs
- **AND** the key file is not published as a build artifact

### Requirement: OTA Signature Verification Check
The firmware CI workflow SHALL run an emulator-based check that exercises OTA signature verification on the `esp32c6` chip target using `esp-emu`, and SHALL fail the check if a correctly signed image is rejected or an incorrectly signed image is accepted. A failure of this check SHALL fail CI rather than be retried or reported as advisory.

#### Scenario: Correctly signed image accepted
- **WHEN** the emulator receives an OTA image signed with the same key as the running firmware
- **THEN** the commit succeeds and the job reports success

#### Scenario: Tampered image rejected
- **WHEN** the emulator receives an image whose bytes were modified after signing
- **THEN** the commit fails, the boot partition is unchanged, and the job reports success for having observed the rejection

#### Scenario: Wrongly-keyed image rejected
- **WHEN** the emulator receives an image signed with a key other than the running firmware's
- **THEN** the commit fails and the job reports success for having observed the rejection

#### Scenario: Emulator failure fails the check
- **WHEN** the emulator run panics, times out, or produces an outcome opposite to the expected one
- **THEN** the job reports failure and the CI check is marked failed

### Requirement: Independent Parallel Jobs
The build check, the unit test check, and the OTA signature verification check SHALL run as independent jobs so any one can fail without blocking or being blocked by the others.

#### Scenario: Test failure does not block build result reporting
- **WHEN** the unit test job fails
- **THEN** the build job still reports its own result independently

#### Scenario: Signature check failure does not block other results
- **WHEN** the OTA signature verification job fails
- **THEN** the build job and the unit test job still report their own results independently
