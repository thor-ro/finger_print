## ADDED Requirements

### Requirement: NVS Namespace Management
The `sdf_storage` component SHALL manage NVS namespaces for credentials, configuration, and security data.

#### Scenario: Namespace structure
- **WHEN** `sdf_storage_init()` called
- **THEN** Open NVS namespace "sdf" (RW)
- **THEN** Open NVS namespace "sdf_keys" (RO, for encryption keys if needed)

### Requirement: Nuki Credentials Persistence
The `sdf_storage` component SHALL store and retrieve Nuki pairing credentials (authorization_id, shared_key).

#### Scenario: Save Nuki credentials
- **WHEN** `sdf_storage_set_nuki_credentials(auth_id, shared_key)` called
- **THEN** Write `auth_id` (8 bytes) to key "nuki_auth_id"
- **THEN** Write `shared_key` (32 bytes) to key "nuki_shared_key"
- **THEN** Write `paired` flag (1 byte) = 1 to key "nuki_paired"

#### Scenario: Load Nuki credentials
- **WHEN** `sdf_storage_get_nuki_credentials(&auth_id, &shared_key)` called
- **THEN** Read "nuki_paired" flag
- **THEN** If 1: read auth_id and shared_key, return success
- **THEN** If 0 or missing: return `SDF_STORAGE_ERR_NOT_FOUND`

#### Scenario: Erase Nuki credentials
- **WHEN** `sdf_storage_erase_nuki_credentials()` called
- **THEN** Delete keys "nuki_auth_id", "nuki_shared_key", "nuki_paired"

### Requirement: BLE Target Address Persistence
The `sdf_storage` component SHALL store and retrieve the Nuki BLE target address.

#### Scenario: Save BLE address
- **WHEN** `sdf_storage_set_nuki_target_addr(addr_type, addr[6])` called
- **THEN** Write address type (1 byte) to "nuki_addr_type"
- **THEN** Write 6-byte address (little-endian) to "nuki_target_addr"

#### Scenario: Load BLE address
- **WHEN** `sdf_storage_get_nuki_target_addr(&addr_type, addr[6])` called
- **THEN** Read both keys, return success if both exist
- **THEN** If missing: return default (all zeros, addr_type=0)

### Requirement: Security Policy Persistence
The `sdf_storage` component SHALL store security configuration (failed attempt count, lockout state, nonce cache).

#### Scenario: Security state persistence
- **WHEN** Failed attempt counter updated
- **THEN** Write `failed_attempt_count` to "sec_fail_count"
- **THEN** Write `failed_attempt_window_start` to "sec_fail_window"
- **WHEN** Lockout active
- **THEN** Write `lockout_until` to "sec_lockout_until"
- **WHEN** Nonce cache updated
- **THEN** Write nonce counter to "sec_nonce_counter"
- **THEN** Write nonce cache array to "sec_nonce_cache"

### Requirement: Encrypted NVS Verification
The `sdf_storage` component SHALL verify NVS encryption is enabled at boot.

#### Scenario: Security status check
- **WHEN** `sdf_storage_get_security_status()` called
- **THEN** Check `nvs_encryption_enabled()` (ESP-IDF API)
- **THEN** Return `SDF_STORAGE_SEC_ENCRYPTED` if enabled
- **THEN** Return `SDF_STORAGE_SEC_UNENCRYPTED` if not (should not happen per policy)
- **THEN** On unencrypted: log error, set Zigbee alarm

### Requirement: Erase All Data
The `sdf_storage` component SHALL provide factory reset erasure of all SDF NVS data.

#### Scenario: Erase all
- **WHEN** `sdf_storage_erase_all()` called
- **THEN** Erase all keys in "sdf" namespace
- **THEN** Return success