## ADDED Requirements

### Requirement: OTA Image Signature Verification
The system SHALL verify Ed25519 signature on OTA image before committing. Signature covers the entire binary image (app + metadata).

#### Scenario: Valid signature
- **WHEN** OTA image has valid Ed25519 signature matching embedded public key
- **THEN** Verification passes, OTA proceeds to commit

#### Scenario: Invalid signature
- **WHEN** OTA image signature verification fails
- **THEN** OTA session aborted, audit event OTA_SIGNATURE_INVALID emitted, partition marked invalid

#### Scenario: Missing signature
- **WHEN** OTA image has no signature appended
- **THEN** OTA session aborted, audit event OTA_SIGNATURE_MISSING emitted

### Requirement: Embedded Public Key
The system SHALL embed Ed25519 public key in firmware at build time for signature verification.

#### Scenario: Key embedded in firmware
- **WHEN** Building firmware with OTA signing enabled
- **THEN** Public key stored in read-only section, accessible to sdf_ota at runtime

### Requirement: Signing Tool Integration
The build system SHALL provide `sdf_sign_ota.py` script that:
1. Takes built binary as input
2. Appends Ed25519 signature (64 bytes) + magic marker (4 bytes: `SDF\1`)
3. Outputs signed binary ready for OTA distribution

#### Scenario: Sign release binary
- **WHEN** Running `sdf_sign_ota.py --input sdf.bin --key ota_private.key --output sdf_signed.bin`
- **THEN** Output binary = original + 64-byte signature + 4-byte magic, verifiable by firmware

#### Scenario: Verify signed binary
- **WHEN** Running `sdf_sign_ota.py --verify --input sdf_signed.bin --pubkey ota_public.key`
- **THEN** Returns success if signature matches, failure otherwise

### Requirement: Bootloader Integration
The bootloader SHALL NOT verify signature (too slow, limited space). Verification happens in app before commit.

#### Scenario: App verifies before commit
- **WHEN** OTA download complete, before esp_ota_end()
- **THEN** sdf_ota_verify_signature() called, result determines commit/abort