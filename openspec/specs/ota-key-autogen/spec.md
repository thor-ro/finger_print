# OTA Key Autogen

## Purpose
Specifies the requirements for automatic generation and management of OTA signing keys to simplify local development while preventing accidental key leaks.

## Requirements

### Requirement: Auto-generation of OTA signing key
The system SHALL automatically generate an Ed25519 private key (`ota_private.key`) and extract its public key during the build process if the private key does not already exist.

#### Scenario: Developer builds firmware without existing key
- **WHEN** the `ota_private.key` file is missing in the project
- **THEN** the CMake build script automatically invokes the key generation tool to create a new `ota_private.key` before proceeding with the build.

#### Scenario: CI builds firmware with injected key
- **WHEN** the `ota_private.key` file already exists (e.g., injected via CI secrets)
- **THEN** the CMake build script uses the existing key and does NOT generate a new one.

### Requirement: Prevent key commits
The repository configuration SHALL prevent OTA private keys from being tracked by version control.

#### Scenario: Developer attempts to commit keys
- **WHEN** a developer generates an `ota_private.key` locally
- **THEN** Git ignores the file according to the `.gitignore` rules.
