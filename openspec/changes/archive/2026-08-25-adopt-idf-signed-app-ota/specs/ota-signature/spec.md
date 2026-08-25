## ADDED Requirements

### Requirement: Trust Anchor Is the Running Firmware's Signing Key
Because hardware secure boot is not enabled, the set of trusted signing keys SHALL be derived from the signature block of the currently running application rather than from eFuse or from a key blob linked into the application. An OTA image SHALL be accepted only if it is signed by the same key as the firmware already installed on the device.

#### Scenario: Image signed with the running firmware's key accepted
- **WHEN** an OTA image signed with the same key as the running firmware is transferred
- **THEN** verification succeeds and the image is committed

#### Scenario: Image signed with a different key rejected
- **WHEN** an OTA image is signed with a well-formed key that does not match the running firmware's signing key
- **THEN** verification fails, an `OTA_SIGNATURE_INVALID` audit event is emitted, and the image is not committed

#### Scenario: Unsigned running firmware cannot accept updates
- **WHEN** the running firmware carries no signature block and an OTA transfer completes
- **THEN** verification fails because no trusted key digest can be resolved
- **AND** the image is not committed

### Requirement: Signed First Image Required at Provisioning
The device provisioning procedure SHALL flash a signed image over the serial/USB interface as the initial firmware. An unsigned initial flash SHALL NOT be treated as a supported configuration, because it leaves the device permanently unable to accept OTA updates until it is reflashed over serial.

#### Scenario: Provisioning flashes a signed image
- **WHEN** a device is provisioned for the first time
- **THEN** the image written over serial carries a valid signature block
- **AND** a subsequent OTA update signed with the same key verifies successfully

### Requirement: Key Rotation Requires a Serial Reflash
The system SHALL treat signing-key rotation as a serial-reflash operation. Only the first signature block of an image SHALL be consulted during OTA verification, so a transitional image carrying signatures from both an old and a new key SHALL NOT be relied upon to rotate keys over the air.

#### Scenario: Rotation over the air is not supported
- **WHEN** an operator needs to move the fleet to a new signing key
- **THEN** the documented procedure is a serial reflash of a signed image carrying the new key
- **AND** no over-the-air path is offered that would accept an image signed by a key other than the running firmware's

### Requirement: Trusted Key Is Not Embedded as a Separate Blob
The system SHALL NOT embed a standalone public key blob in the firmware image or link one via a read-only section. The public key used for verification SHALL be carried inside the signature block appended to the application image, and the SHA-256 digest of that key SHALL be what establishes trust.

#### Scenario: No standalone key blob is linked
- **WHEN** firmware is built with signed-app verification enabled
- **THEN** no separate public-key binary is embedded into the application
- **AND** verification resolves its trusted key digest from the running application's signature block

### Requirement: Verification Gates the Commit
The bootloader SHALL NOT verify the OTA signature. Verification SHALL occur in the application as part of finalizing the transfer, and its result SHALL determine whether the image is committed. A failing image SHALL NOT be copied from a staging partition to its final partition, and SHALL NOT become the boot partition. Reading the staging partition back to compute the digest and locate the signature block is permitted, provided it occurs after the transfer's trailing bytes have been flushed and before any staging-to-final copy.

#### Scenario: Verification precedes commit
- **WHEN** an OTA transfer completes
- **THEN** signature verification runs before the image is made bootable
- **AND** the boot partition is switched only if verification passes

#### Scenario: Bad image is never finalized
- **WHEN** an image with an invalid or missing signature is transferred
- **THEN** no staging-to-final copy is performed for that image
- **AND** the boot partition is not switched

#### Scenario: Verification succeeds with flash encryption enabled
- **WHEN** firmware is built with `CONFIG_SECURE_FLASH_ENC_ENABLED=y` and a validly signed image is transferred
- **THEN** the signature verifies
- **AND** the outcome is identical to the same transfer with flash encryption disabled

#### Scenario: Failure is reported as a signature failure
- **WHEN** finalizing the transfer reports a validation failure
- **THEN** the session is aborted, the session's resources are released, and an `OTA_SIGNATURE_INVALID` audit event is emitted

### Requirement: Verification Is Unconditional
Signed-app verification SHALL be unconditional. The system SHALL NOT provide a build configuration that compiles out signature verification, and SHALL NOT contain a verification stub that reports success when verification is unavailable.

#### Scenario: No opt-out exists
- **WHEN** the firmware's configuration options are inspected
- **THEN** there is no option that disables OTA signature verification
- **AND** no code path returns success on the grounds that verification is disabled

#### Scenario: Unsigned image rejected
- **WHEN** an unsigned image is transferred to a device
- **THEN** the OTA session is aborted and the image is not committed

## MODIFIED Requirements

### Requirement: Signature Algorithm
The system SHALL verify OTA images using the ESP-IDF Secure Boot V2 ECDSA signature scheme over the NIST P-256 curve (`secp256r1`), as provided by ESP-IDF's `bootloader_support` component. The system SHALL NOT implement its own signature verification, and SHALL NOT vendor or link any cryptographic library for this purpose beyond what ESP-IDF already provides.

#### Scenario: Verification uses already-linked primitives
- **WHEN** firmware is built with signed-app verification enabled
- **THEN** the build succeeds using only ESP-IDF's Secure Boot V2 signature support
- **AND** no project-owned signature verification code is compiled into the firmware
- **AND** no additional third-party cryptographic library is vendored into the firmware

#### Scenario: Hardware secure boot remains disabled
- **WHEN** firmware is built for the current development configuration
- **THEN** signed-app verification is enabled without hardware secure boot
- **AND** no eFuse is burned by building, flashing, or updating the device

### Requirement: Signed Payload Is a SHA-256 Digest
The signature SHALL be computed over the SHA-256 digest of the image bytes padded up to the next 4096-byte boundary, excluding the appended signature sector. Verification SHALL NOT require the complete image to be resident in memory, and SHALL NOT impose any maximum verifiable image size.

#### Scenario: Image larger than available RAM verifies
- **WHEN** an OTA image whose size exceeds the device's available heap is transferred and its signature is valid
- **THEN** verification succeeds and the OTA proceeds to commit
- **AND** no allocation proportional to the image size is performed

#### Scenario: Digest covers image bytes only
- **WHEN** a signed image is transferred
- **THEN** the digest is computed over the image bytes padded to the next 4096-byte boundary
- **AND** the appended signature sector is excluded from the digest

### Requirement: Signed Image Format
A signed image SHALL consist of the application image padded up to the next 4096-byte boundary, followed by a 4096-byte ESP-IDF Secure Boot V2 signature sector. The project SHALL NOT define its own signature footer format or magic marker.

#### Scenario: Well-formed signed image accepted
- **WHEN** an image carries a valid Secure Boot V2 ECDSA signature block signed by the trusted key
- **THEN** the signature is verified against the image digest and the image is committed

#### Scenario: Missing signature rejected
- **WHEN** an OTA image carries no valid signature block at the expected offset
- **THEN** the OTA session is aborted and the image is not committed

#### Scenario: Invalid signature rejected
- **WHEN** an OTA image's signature does not verify against the trusted key
- **THEN** the OTA session is aborted, an `OTA_SIGNATURE_INVALID` audit event is emitted, and the image is not committed

#### Scenario: Declared transfer size accounts for the signature sector
- **WHEN** a client begins a transfer of a signed image
- **THEN** the declared image size is the size of the padded image plus its signature sector
- **AND** the transfer completes without the transport needing to interpret the signature format

### Requirement: Signing Tool
The build system SHALL sign release binaries using ESP-IDF's own signing tooling rather than a project-owned signing script. The project SHALL NOT maintain a bespoke signing or signature-verification utility.

#### Scenario: Sign a release binary
- **WHEN** an operator signs a built binary using the documented ESP-IDF signing procedure and the project's signing key
- **THEN** the output is verifiable by a device running firmware signed with the same key

#### Scenario: Verify a signed binary
- **WHEN** an operator verifies a signed binary using ESP-IDF's signature verification tooling
- **THEN** the command exits successfully if the signature is valid, and non-zero otherwise

## REMOVED Requirements

### Requirement: Streaming Digest Computation
**Reason**: The image digest is now computed by ESP-IDF while finalizing the transfer, by reading back the staging partition in bounded chunks. The project no longer accumulates a digest as bytes arrive, so there is no chunk-splitting or resume behavior of its own to specify. The properties this requirement protected — no allocation proportional to image size, no maximum verifiable image size — are retained under "Signed Payload Is a SHA-256 Digest".

**Migration**: None required for clients. The transfer protocol is unchanged; only where the digest is computed moves.

### Requirement: Signature Footer Captured From Transfer Stream
**Reason**: There is no project-defined footer to capture. ESP-IDF locates and reads the signature block itself, from the staging partition, after the transfer's trailing bytes have been flushed and before any staging-to-final copy — which is the readback ordering this requirement originally existed to avoid getting wrong.

**Migration**: None required for clients. Transfer-window capture is retained for the application descriptor; see "Application Descriptor Captured From Transfer Stream", which is unchanged.

### Requirement: Verification Core Is Independently Testable
**Reason**: The project no longer owns a verification core. ESP-IDF's implementation lives in `bootloader_support`, which is not built for `IDF_TARGET=linux`, so it cannot be exercised by the host test runner and does not need to be — it is not project-maintained code.

**Migration**: Coverage of the verification path moves to an on-target/emulator test asserting that a tampered image is rejected at commit. Host coverage of transfer-window capture is retained; see "Window Capture Is Independently Testable", which is unchanged.

### Requirement: Embedded Public Key
**Reason**: No standalone public key is embedded any more. The verifying key travels inside the image's own signature block, and trust is established by matching its digest against the running application's. Replaced by "Trusted Key Is Not Embedded as a Separate Blob" and "Trust Anchor Is the Running Firmware's Signing Key".

**Migration**: The build stops producing and embedding `ota_public_key.bin`; operators hold only the PEM signing key.

### Requirement: Verification Occurs In-App Before Commit
**Reason**: The prohibition on reading the target partition between `esp_ota_begin()` and `esp_ota_end()` is incompatible with ESP-IDF's implementation, which reads the staging partition back inside `esp_ota_end()` to compute the digest. The safety property that prohibition protected — a bad image is never finalized — is preserved and restated in "Verification Gates the Commit", which permits the readback under the ordering constraints that make it correct.

**Migration**: None required for clients. Verification moves from before the finalize call to inside it; the commit still happens only on success.

### Requirement: Verification Enabled By Default
**Reason**: Verification is no longer a default that can be overridden — `CONFIG_SDF_OTA_SIGNATURE_VERIFY` and its fail-open stub are removed entirely. Replaced by "Verification Is Unconditional".

**Migration**: Any build or CI configuration setting `CONFIG_SDF_OTA_SIGNATURE_VERIFY` must drop it; the option no longer exists.
