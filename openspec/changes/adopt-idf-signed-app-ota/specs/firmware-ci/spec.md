## ADDED Requirements

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

## MODIFIED Requirements

### Requirement: Independent Parallel Jobs
The build check, the unit test check, and the OTA signature verification check SHALL run as independent jobs so any one can fail without blocking or being blocked by the others.

#### Scenario: Test failure does not block build result reporting
- **WHEN** the unit test job fails
- **THEN** the build job still reports its own result independently

#### Scenario: Signature check failure does not block other results
- **WHEN** the OTA signature verification job fails
- **THEN** the build job and the unit test job still report their own results independently
