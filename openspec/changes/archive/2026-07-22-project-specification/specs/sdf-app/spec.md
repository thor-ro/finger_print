## ADDED Requirements

### Requirement: Biometric Unlock Flow
The `sdf_app` component SHALL orchestrate the biometric unlock flow: when a fingerprint match occurs with a non-admin user, trigger a Nuki BLE unlock action and update Zigbee lock state.

#### Scenario: Successful biometric unlock
- **WHEN** `sdf_services` invokes `unlock_cb` with a matched user ID (permission != 3)
- **THEN** `sdf_app` calls `sdf_lock_flow_begin(UNLATCH)` to start the Nuki challenge-response flow
- **THEN** on successful completion, `sdf_app` calls `sdf_protocol_zigbee_update_lock_state(UNLOCKED)` to report state
- **THEN** audit event `SDF_AUDIT_BIOMETRIC_MATCH` is emitted with user_id

#### Scenario: Biometric unlock with BLE failure
- **WHEN** `sdf_lock_flow_begin` returns error or times out
- **THEN** `sdf_app` emits `SDF_AUDIT_AUTH_LOCKOUT` audit event
- **THEN** `sdf_app` calls `sdf_protocol_zigbee_update_lock_state(UNKNOWN)` to report failure
- **THEN** lock action is retried up to `CONFIG_SDF_APP_LOCK_ACTION_MAX_RETRIES` times (default 2)

### Requirement: Zigbee Bridge Flow
The `sdf_app` component SHALL translate Zigbee Door Lock Cluster commands (Lock, Unlock, Latch) into Nuki BLE lock actions and report resulting lock state back to Zigbee.

#### Scenario: Remote unlock via Zigbee
- **WHEN** Zigbee coordinator sends `Unlock Door` command (cluster 0x0101, command 0x00)
- **THEN** `sdf_protocol_zigbee` invokes `command_cb(UNLOCK)`
- **THEN** `sdf_app` calls `sdf_lock_flow_begin(UNLOCK)`
- **THEN** on completion, `sdf_app` updates internal lock state and calls `sdf_protocol_zigbee_update_lock_state(UNLOCKED)`

#### Scenario: Remote lock via Zigbee
- **WHEN** Zigbee coordinator sends `Lock Door` command
- **THEN** `sdf_app` calls `sdf_lock_flow_begin(LOCK)`
- **THEN** on completion, Zigbee lock state updated to `LOCKED`

#### Scenario: Latch command via Zigbee
- **WHEN** Zigbee coordinator sends `Latch Door` command
- **THEN** `sdf_app` calls `sdf_lock_flow_begin(UNLATCH)`
- **THEN** on completion, Zigbee lock state updated to `UNLATCHED`

### Requirement: Nuki Pairing Flow
The `sdf_app` component SHALL manage the Nuki pairing process: initiate pairing on double-press, require admin authorization, exchange ECDH keys, and store credentials in encrypted NVS.

#### Scenario: Successful Nuki pairing
- **WHEN** User double-presses configuration button
- **THEN** `sdf_app` sets `pending_admin_action = PAIR_NUKI`, LED pulses yellow
- **WHEN** Admin fingerprint matched (permission == 3) within 10s
- **THEN** `sdf_app` calls `sdf_protocol_ble_start_pairing()`
- **THEN** On pairing success, credentials (auth_id, shared_key) saved to NVS via `sdf_storage`
- **THEN** LED solid green, `SDF_AUDIT_PAIRING_COMPLETE` emitted

#### Scenario: Pairing timeout
- **WHEN** No admin fingerprint within 10 seconds of double-press
- **THEN** `sdf_app` clears `pending_admin_action`, LED red flash, `SDF_AUDIT_AUTH_LOCKOUT` emitted

### Requirement: Enrollment Trigger
The `sdf_app` component SHALL queue enrollment requests from button press (short press) or Zigbee programming commands, and invoke `sdf_services_request_enrollment()`.

#### Scenario: Local enrollment trigger
- **WHEN** Short button press detected
- **THEN** If unclaimed (0 users), `sdf_app` calls `sdf_services_request_enrollment(user_id=1, permission=3)`
- **THEN** If claimed (>0 users), `sdf_app` sets `pending_admin_action = ENROLL`, LED pulses blue

#### Scenario: Remote enrollment trigger via Zigbee
- **WHEN** Zigbee `Set PIN Code` or `Set RFID Code` command received
- **THEN** `sdf_app` queues enrollment request with parameters from command
- **THEN** Admin authorization required before enrollment starts

### Requirement: Lock Flow Manager
The `sdf_app` component SHALL implement a challenge-response state machine for Nuki lock actions: send challenge, receive nonce, compute authenticator, send encrypted lock action, verify status.

#### Scenario: Complete lock action sequence
- **WHEN** `sdf_lock_flow_begin(action)` called
- **THEN** State machine: IDLE → CHALLENGE_SENT → NONCE_RECEIVED → ACTION_SENT → STATUS_RECEIVED → COMPLETE
- **THEN** Each transition has timeout (challenge: 5s, nonce: 3s, action: 5s, status: 3s)
- **THEN** On timeout or error, retry up to max_retries (default 2), then FAIL

### Requirement: BLE Transport Manager
The `sdf_app` component SHALL gate the NimBLE radio: enable only during active lock action or pairing, disable after keyturner state sync.

#### Scenario: BLE radio enabled for lock action
- **WHEN** `sdf_lock_flow_begin()` called
- **THEN** `sdf_protocol_ble_enable()` called, BLE radio starts
- **THEN** After lock action completes (success or failure), `sdf_protocol_ble_disable()` called after keyturner sync

#### Scenario: BLE radio enabled for pairing
- **WHEN** `sdf_protocol_ble_start_pairing()` called
- **THEN** BLE radio stays enabled for pairing duration
- **THEN** Disabled after pairing completes or times out

### Requirement: Audit Event Emission
The `sdf_app` component SHALL emit audit events for security-relevant actions via callback `sdf_app_set_audit_callback()`.

#### Scenario: Audit events emitted
- **WHEN** Biometric match: `SDF_AUDIT_BIOMETRIC_MATCH` with user_id, permission
- **WHEN** Biometric fail: `SDF_AUDIT_BIOMETRIC_FAILED` with user_id, attempt_count
- **WHEN** Biometric lockout: `SDF_AUDIT_AUTH_LOCKOUT` with lockout_duration_s
- **WHEN** Nonce replay detected: `SDF_AUDIT_NONCE_REPLAY` with nonce
- **WHEN** Protocol error: `SDF_AUDIT_PROTOCOL_ERROR` with error_code
- **WHEN** Pairing complete: `SDF_AUDIT_PAIRING_COMPLETE` with auth_id
- **WHEN** Pairing failed: `SDF_AUDIT_PAIRING_FAILED` with reason

### Requirement: Factory Reset (Incomplete - Technical Debt)
The `sdf_app` component SHALL provide a factory reset admin action that clears all NVS data, fingerprint templates, and Zigbee network state. **NOTE: Currently has TODO in implementation.**

#### Scenario: Factory reset initiated
- **WHEN** Admin authorizes `FACTORY_RESET` action (triple button press or Zigbee command)
- **THEN** `sdf_app` calls `sdf_storage_erase_all()` to clear NVS
- **THEN** `sdf_app` calls `sdf_drivers_fingerprint_delete_all_users()`
- **THEN** `sdf_app` calls `sdf_protocol_zigbee_factory_reset()`
- **THEN** Device reboots into unclaimed state (LED breathes white)

---

### Requirement: Configuration and Build Profiles
The `sdf_app` component SHALL support debug and release build profiles via `sdkconfig.defaults` and `sdkconfig.debug.defaults`/`sdkconfig.release.defaults`.

#### Scenario: Debug build
- **WHEN** Building with `SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.debug.defaults"`
- **THEN** `CONFIG_LOG_DEFAULT_LEVEL=4` (DEBUG), `CONFIG_SDF_APP_DEBUG_LOCK_FLOW=1`
- **THEN** Verbose logging for lock flow state transitions

#### Scenario: Release build
- **WHEN** Building with `SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.release.defaults"`
- **THEN** `CONFIG_LOG_DEFAULT_LEVEL=2` (WARN), optimizations enabled
- **THEN** `CONFIG_SDF_APP_DEBUG_LOCK_FLOW=0`