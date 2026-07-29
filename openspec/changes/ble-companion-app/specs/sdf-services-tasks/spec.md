## ADDED Requirements

### Requirement: Web Registration Authorization
The `sdf_admin_task` and `sdf_services` SHALL support an "Admin Auth" state where it waits for an Admin fingerprint scan to authorize a pending Web Registration request over BLE GATT.

#### Scenario: Web Registration Authorized
- **WHEN** GATT server requests web registration authorization
- **AND** Admin finger is scanned successfully
- **THEN** system authorizes the registration
- **AND** GATT server saves credentials to NVS

#### Scenario: Web Registration Denied
- **WHEN** GATT server requests web registration authorization
- **AND** non-Admin finger is scanned (or timeout occurs)
- **THEN** system denies the registration
