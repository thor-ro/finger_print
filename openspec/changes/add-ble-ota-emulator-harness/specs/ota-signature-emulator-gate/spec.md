## ADDED Requirements

### Requirement: Hardware-Free Signature Gate

The project SHALL provide an automated check that OTA signature verification is active, runnable on `esp-emu` for the `esp32c6` target with no physical device attached, suitable for use as a required CI check.

#### Scenario: Runs without hardware

- **WHEN** the gate is invoked in an environment with `esp-emu` installed and no ESP32-C6 board connected
- **THEN** it builds the fixture, boots it under the emulator, executes every case, and terminates on its own

#### Scenario: Failure is loud

- **WHEN** any case fails, the fixture panics, or the emulator run exceeds its timeout
- **THEN** the gate exits non-zero and the emulator output identifying the failing case is present in its output

#### Scenario: Success is unambiguous

- **WHEN** every case passes
- **THEN** the gate exits zero, and a passing exit is reachable only by executing all three signature cases — a fixture that skipped or never reached them SHALL exit non-zero

#### Scenario: The gate is observed failing before it is trusted

- **WHEN** the gate is established
- **THEN** it has been shown to exit non-zero both for an inverted assertion and for a build with signature verification disabled, so that a gate that can never go red is not mistaken for a passing one

### Requirement: Transport-Independent Verification Cases

The gate SHALL exercise signature verification through the project's own OTA session API rather than through any transport, so that the check is independent of BLE, Wi-Fi and CLI availability under emulation.

#### Scenario: Driven through the OTA session API

- **WHEN** a case runs
- **THEN** it calls `sdf_ota_begin()`, `sdf_ota_write()` and `sdf_ota_verify_and_commit()` in the same order the companion transport calls them, reading image bytes from a pre-staged source rather than receiving them over a link

#### Scenario: Images are pre-staged in flash

- **WHEN** the fixture needs its test images
- **THEN** they are present in the merged flash image the emulator boots, so no transfer protocol participates in the check

### Requirement: Correctly Signed Image Accepted

An image signed with the same key as the running fixture SHALL be accepted at commit.

#### Scenario: Valid image commits

- **WHEN** a correctly signed image is written and committed
- **THEN** `sdf_ota_verify_and_commit()` returns success and the boot partition is updated to the staging partition

### Requirement: Tampered Image Rejected

An image that is signed and then modified SHALL be rejected at commit, and the device SHALL continue to boot the prior image. The rejection SHALL be attributable to signature verification rather than to the image format's own corruption checks.

#### Scenario: Post-signing modification is rejected

- **WHEN** an image is signed, a byte in a loaded segment is flipped, the segment checksum and appended SHA-256 are recomputed so the image is internally consistent, and the result is written and committed
- **THEN** `sdf_ota_verify_and_commit()` fails, an `OTA_SIGNATURE_INVALID` audit event is emitted, and the boot partition is unchanged

#### Scenario: Rejection is not the image checksum in disguise

- **WHEN** the tampered image is verified
- **THEN** it passes the image header, segment, checksum and appended-hash checks, and fails only at signature verification — so an image whose checksum was left broken SHALL NOT be accepted as satisfying this requirement

#### Scenario: The tampered image is accepted when verification is compiled out

- **WHEN** the same tampered image is written and committed by a build with signature verification disabled
- **THEN** it is accepted, demonstrating that its rejection in the shipping configuration is caused by signature verification and nothing else

### Requirement: Image Signed With A Different Key Rejected

An image signed with a well-formed key that is not the running image's key SHALL be rejected at commit.

#### Scenario: Foreign key is rejected

- **WHEN** an image signed with a separately generated P-256 key is written and committed
- **THEN** `sdf_ota_verify_and_commit()` fails and the boot partition is unchanged

#### Scenario: Rejection is attributable to the key, not to malformation

- **WHEN** the foreign-key image is inspected before the run
- **THEN** it carries a structurally valid signature block, so its rejection demonstrates trust-anchor enforcement rather than a parse failure

### Requirement: Fixtures Bound To The Running Image's Key

Because the trust anchor with secure boot disabled is the running application's own signature block, the gate's fixtures SHALL be produced against the same key as the fixture image itself.

#### Scenario: Accept case shares the running key

- **WHEN** the accept-case image is generated
- **THEN** it is signed with the same key used to sign the fixture app that the emulator boots

#### Scenario: Key mismatch surfaces as a gate failure, not a false pass

- **WHEN** the fixture image and the accept-case image are signed with different keys
- **THEN** the accept case fails and the gate exits non-zero, rather than reporting a pass on the reject cases alone

### Requirement: Session Recovery After Rejection

A transfer rejected for a signature failure SHALL leave the device able to start a subsequent OTA.

#### Scenario: Reject then accept

- **WHEN** a tampered image is rejected and a correctly signed image is then transferred in the same boot
- **THEN** the second transfer begins and commits successfully
