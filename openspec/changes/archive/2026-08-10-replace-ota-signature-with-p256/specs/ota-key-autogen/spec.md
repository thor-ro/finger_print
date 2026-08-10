## MODIFIED Requirements

### Requirement: Auto-generation of OTA signing key
The system SHALL automatically generate an ECDSA P-256 (`secp256r1`) private key (`ota_private.key`) and extract its public key during the build process if the private key does not already exist. The extracted public key SHALL be a 65-byte uncompressed EC point (`0x04 || X || Y`).

#### Scenario: Developer builds firmware without existing key
- **WHEN** the `ota_private.key` file is missing in the project
- **THEN** the CMake build script automatically invokes the key generation tool to create a new P-256 `ota_private.key` before proceeding with the build.

#### Scenario: CI builds firmware with injected key
- **WHEN** the `ota_private.key` file already exists (e.g., injected via CI secrets)
- **THEN** the CMake build script uses the existing key and does NOT generate a new one.

#### Scenario: Extracted public key is uncompressed
- **WHEN** the build extracts the public key from `ota_private.key` for embedding
- **THEN** the extracted key is 65 bytes in uncompressed form, not a 32-byte raw key
