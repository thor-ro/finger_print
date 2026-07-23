## ADDED Requirements

### Requirement: Nuki BLE Client State Machine
The `sdf_protocol_ble` component SHALL implement a Nuki BLE client state machine for pairing and lock actions over the Nuki proprietary protocol.

#### Scenario: Pairing handshake
- **WHEN** `sdf_protocol_ble_start_pairing()` called
- **THEN** State: IDLE → SCANNING → CONNECTING → AUTH_REQUESTED → KEY_EXCHANGE → PAIRED
- **THEN** Scan for Nuki device (or use pre-configured BLE address)
- **THEN** Connect, discover Nuki service (UUID 0000180D-0000-1000-8000-00805F9B34FB)
- **THEN** Send pairing request (cmd 0x01), receive auth ID
- **THEN** ECDH key exchange (Curve25519), compute shared secret
- **THEN** Derive authorization data, send (cmd 0x03)
- **THEN** On success: store `authorization_id` and `shared_key` to NVS via `sdf_storage`
- **THEN** Emit `SDF_AUDIT_PAIRING_COMPLETE`

#### Scenario: Lock action sequence
- **WHEN** `sdf_protocol_ble_send_lock_action(action, callback)` called
- **THEN** If not paired: error `SDF_NUKI_ERR_NOT_PAIRED`
- **THEN** Enable BLE radio, connect to Nuki
- **THEN** Send challenge request (cmd 0x05), receive 32-byte nonce
- **THEN** Compute authenticator: HMAC-SHA256(shared_key, nonce + action + auth_id)
- **THEN** Encrypt lock action payload (lib sodium secretbox)
- **THEN** Send lock action (cmd 0x0D) with encrypted payload
- **THEN** Receive status response, invoke callback with result
- **THEN** Disconnect, disable BLE radio

### Requirement: Message Framing
The `sdf_protocol_ble` component SHALL handle Nuki protocol message framing: command IDs, payload length, CRC16, encryption envelopes.

#### Scenario: Frame encoding/decoding
- **WHEN** Sending command
- **THEN** Encode: [CMD_ID:1][LEN:1][PAYLOAD:N][CRC16:2]
- **THEN** For encrypted frames: [CMD_ID:1][LEN:1][ENCRYPTED:N][CRC16:2] where encrypted = secretbox(payload, nonce, key)
- **WHEN** Receiving response
- **THEN** Validate CRC, decrypt if encrypted flag set, parse payload

### Requirement: Curve25519 ECDH and Crypto
The `sdf_protocol_ble` component SHALL implement Curve25519 ECDH key exchange, HMAC-SHA256, and libsodium secretbox (XSalsa20-Poly1305).

#### Scenario: Key exchange during pairing
- **WHEN** Pairing initiated
- **THEN** Generate ephemeral Curve25519 keypair
- **THEN** Send public key in auth request
- **THEN** Receive Nuki public key
- **THEN** Compute shared secret = X25519(ephemeral_priv, nuki_pub)
- **THEN** Derive `shared_key` = HKDF-SHA256(shared_secret, "nuki-pairing", auth_id)
- **THEN** Store `shared_key` (32 bytes) and `authorization_id` (8 bytes) to NVS

#### Scenario: Authenticator computation
- **WHEN** Lock action requires authenticator
- **THEN** `authenticator = HMAC-SHA256(shared_key, nonce || action_byte || auth_id)`
- **THEN** First 8 bytes used as authenticator in lock action payload

#### Scenario: Encrypted payload
- **WHEN** Lock action payload encrypted
- **THEN** nonce = 24-byte counter (incrementing per action)
- **THEN** ciphertext = secretbox(payload, nonce, shared_key)
- **THEN** Store nonce counter to NVS for replay protection

### Requirement: Nonce Replay Protection
The `sdf_protocol_ble` component SHALL maintain a bounded cache of recently seen nonces to prevent replay attacks.

#### Scenario: Nonce cache
- **WHEN** Nonce received from Nuki in challenge response
- **THEN** Check against `nonce_cache` (size `CONFIG_SDF_NUKI_NONCE_WINDOW`, default 8)
- **THEN** If found: reject with `SDF_NUKI_RESULT_ERR_NONCE_REUSE`, emit `SDF_AUDIT_NONCE_REPLAY`
- **THEN** If not found: add to cache, evict oldest if full
- **WHEN** Lock action sent
- **THEN** Store sent nonce counter to NVS for persistence across reboots

### Requirement: BLE Radio Gating
The `sdf_protocol_ble` component SHALL provide enable/disable API for BLE radio gating controlled by `sdf_app` and `sdf_power`.

#### Scenario: Radio enable
- **WHEN** `sdf_protocol_ble_enable()` called
- **THEN** NimBLE stack initialized if not already
- **THEN** Start advertising/scanning as needed

#### Scenario: Radio disable
- **WHEN** `sdf_protocol_ble_disable()` called
- **THEN** Disconnect any active connection
- **THEN** Stop advertising/scanning
- **THEN** Deinitialize NimBLE controller to save power

### Requirement: Nuki Discovery (All-Zero Address)
The `sdf_protocol_ble` component SHALL support discovery mode when BLE target address is all zeros.

#### Scenario: Discovery mode
- **WHEN** `SDF_NUKI_TARGET_ADDR` = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00}
- **THEN** During pairing: scan for Nuki service UUID, connect to first found
- **THEN** On lock action: scan, connect, execute action
- **THEN** Allows pairing without pre-configured lock MAC address

### Requirement: Credential Persistence
The `sdf_protocol_ble` component SHALL load/save Nuki credentials via `sdf_storage` API.

#### Scenario: Load credentials on init
- **WHEN** `sdf_protocol_ble_init()` called
- **THEN** Call `sdf_storage_get_nuki_credentials(&auth_id, &shared_key)`
- **THEN** If found: set paired state, ready for lock actions
- **THEN** If not found: unpaired state, await pairing

#### Scenario: Save credentials on pairing
- **WHEN** Pairing completes successfully
- **THEN** Call `sdf_storage_set_nuki_credentials(auth_id, shared_key)`
- **THEN** On success: transition to paired state