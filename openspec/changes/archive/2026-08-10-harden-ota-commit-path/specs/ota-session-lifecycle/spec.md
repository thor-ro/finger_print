## ADDED Requirements

### Requirement: Failed Sessions Release Everything They Hold
When any `sdf_ota` operation on an active session returns an error, the OTA component SHALL release the resources that session holds before returning: the digest context, the underlying `esp_ota_handle_t` if still open, and the session's active flag. Release SHALL NOT be delegated to the caller.

#### Scenario: Failed commit leaves no session active
- **WHEN** `sdf_ota_verify_and_commit()` returns any error
- **THEN** the session is no longer active and holds no open OTA handle
- **AND** a subsequent `sdf_ota_begin()` succeeds without an intervening reboot

#### Scenario: Failed write leaves no session active
- **WHEN** `sdf_ota_write()` returns any error
- **THEN** the session is no longer active and holds no open OTA handle

#### Scenario: Failed integrity check leaves no session active
- **WHEN** `sdf_ota_verify_integrity()` returns any error
- **THEN** the session is no longer active and holds no open OTA handle

#### Scenario: Caller that does not abort does not wedge OTA
- **WHEN** a transport handler observes an error and discards its session reference without calling `sdf_ota_abort()`
- **THEN** OTA remains available to every transport
- **AND** no `esp_ota` allocation is leaked

### Requirement: Redundant Abort Is Safe
`sdf_ota_abort()` on a session that has already been closed by an internal failure path SHALL be a harmless no-op returning `ESP_ERR_INVALID_STATE`, and SHALL NOT release any resource a second time.

#### Scenario: Abort after a failed commit
- **WHEN** a caller calls `sdf_ota_abort()` after `sdf_ota_verify_and_commit()` has returned an error
- **THEN** the call returns `ESP_ERR_INVALID_STATE`
- **AND** no double-free or double-abort of the underlying OTA handle occurs

### Requirement: OTA Handle Ownership Is Tracked Explicitly
The session SHALL track whether the underlying `esp_ota_handle_t` is still owned by the session. The handle SHALL be considered released once `esp_ota_abort()` has been called on it, or once `esp_ota_end()` has returned any value, because `esp_ota_end()` frees its internal entry on both success and failure.

#### Scenario: Failure after esp_ota_end does not abort a freed handle
- **WHEN** `esp_ota_end()` or `esp_ota_set_boot_partition()` fails during commit
- **THEN** the session is closed and marked inactive
- **AND** `esp_ota_abort()` is not called on the already-freed handle
