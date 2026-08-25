## ADDED Requirements

### Requirement: Auto-generation of the Image Signing Key
The system SHALL automatically generate an ECDSA P-256 (`secp256r1`) private signing key in PEM format during the build process if it does not already exist, and SHALL use it to sign the built application image. The build SHALL NOT extract or embed a standalone public key blob, because the public key is carried inside the image's signature block.

#### Scenario: Developer builds firmware without an existing key
- **WHEN** the OTA signing key file is missing in the project
- **THEN** the build automatically generates a new P-256 signing key in PEM format before proceeding with the build

#### Scenario: CI builds firmware with an injected key
- **WHEN** the OTA signing key file already exists (e.g. injected via CI secrets)
- **THEN** the build uses the existing key and does NOT generate a new one

#### Scenario: No public key artifact is produced
- **WHEN** the build completes
- **THEN** no standalone public-key binary is generated or embedded into the application

#### Scenario: Regenerated key does not silently break updates
- **WHEN** the signing key is regenerated while devices in the field run firmware signed by the previous key
- **THEN** images signed with the new key are rejected by those devices
- **AND** the build surfaces that the key was newly generated rather than reused

## MODIFIED Requirements

### Requirement: Prevent key commits
The repository configuration SHALL prevent OTA signing keys from being tracked by version control, and SHALL NOT leave superseded key artifacts tracked after the signing scheme changes.

#### Scenario: Developer attempts to commit keys
- **WHEN** a developer's build generates an OTA signing key locally
- **THEN** Git ignores the file according to the `.gitignore` rules

#### Scenario: Superseded key artifacts are untracked
- **WHEN** the repository is inspected after the signing scheme change
- **THEN** no key material belonging to the retired custom footer scheme remains tracked

## REMOVED Requirements

### Requirement: Auto-generation of OTA signing key
**Reason**: The generated key's consumer changes. The retired scheme required extracting a 65-byte uncompressed public point from the private key and embedding it into the firmware via `EMBED_FILES`; ESP-IDF's signing tooling consumes the PEM key directly and needs no extracted public artifact. Replaced by "Auto-generation of the Image Signing Key".

**Migration**: Delete `ota_private.key`, `ota_public.key`, `ota_public_key.bin`, and `ota_public_key.bin.pem`; the next build generates a PEM signing key in their place. Devices running firmware signed by the old key must be reflashed over serial, since the trust anchor is the running firmware's own key.
