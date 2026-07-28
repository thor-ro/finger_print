## ADDED Requirements

### Requirement: Version String from Git Tag
The system SHALL embed firmware version string derived from git tag at build time in format: `v<major>.<minor>.<patch>[-<commit-count>-g<short-hash>]`

#### Scenario: Release tag build
- **WHEN** Building from tag `v1.2.3` (clean tree)
- **THEN** Version string = `v1.2.3`

#### Scenario: Development build
- **WHEN** Building from commit 5 after tag `v1.2.3` with hash `abc1234`
- **THEN** Version string = `v1.2.3-5-gabc1234`

#### Scenario: No tags (initial development)
- **WHEN** Building from repository with no tags
- **THEN** Version string = `v0.0.0-<commit-count>-g<short-hash>`

### Requirement: Version Accessible at Runtime
The system SHALL expose version string via:
1. `esp_app_get_description()->version` (standard ESP-IDF)
2. `sdf_ota_get_version()` returning `const char*`
3. CLI command `ota version` printing full version string

#### Scenario: Runtime version query
- **WHEN** `sdf_ota_get_version()` called
- **THEN** Returns pointer to version string stored in app description

#### Scenario: CLI version command
- **WHEN** User runs `ota version`
- **THEN** Output: `SDF Firmware v1.2.3-5-gabc1234 (built 2026-07-23 14:30:00)`

### Requirement: Version Comparison Logic
The system SHALL implement semantic version comparison for OTA decision making:
- Compare major, minor, patch numerically
- Pre-release suffixes (commit count, hash) considered lower than release
- Build metadata ignored for comparison

#### Scenario: Compare v1.2.3 vs v1.2.4
- **WHEN** Current=v1.2.3, Incoming=v1.2.4
- **THEN** Comparison result: INCOMING_GREATER (upgrade)

#### Scenario: Compare v1.2.3-5-gabc vs v1.2.3
- **WHEN** Current=v1.2.3-5-gabc, Incoming=v1.2.3
- **THEN** Comparison result: INCOMING_GREATER (release > pre-release)

#### Scenario: Compare v1.2.3 vs v1.2.3-10-gdef
- **WHEN** Current=v1.2.3, Incoming=v1.2.3-10-gdef
- **THEN** Comparison result: INCOMING_LOWER (pre-release < release)