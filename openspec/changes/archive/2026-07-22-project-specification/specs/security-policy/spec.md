## ADDED Requirements

### Requirement: Biometric Rate Limiting
The security policy SHALL enforce rate limiting on failed biometric attempts.

#### Scenario: Failed attempt threshold
- **WHEN** Failed fingerprint match occurs (no match or sensor error)
- **THEN** Increment `failed_attempt_count`
- **THEN** If first failure in window: record `window_start = now`
- **THEN** If `failed_attempt_count >= 5` AND `now - window_start <= 60000ms`
- **THEN** Trigger lockout

#### Scenario: Lockout duration
- **WHEN** Lockout triggered
- **THEN** Set `lockout_until = now + 120000ms` (2 minutes)
- **THEN** Ignore all fingerprint matches until `now >= lockout_until`
- **THEN** Set Zigbee alarm bit 0x0004 (BIOMETRIC_LOCKOUT)

#### Scenario: Lockout expiry
- **WHEN** `now >= lockout_until`
- **THEN** Clear `failed_attempt_count = 0`
- **THEN** Clear Zigbee alarm bit 0x0004
- **THEN** Resume normal match cycle

### Requirement: Nonce Replay Protection
The security policy SHALL prevent replay attacks on Nuki BLE communication.

#### Scenario: Nonce cache
- **WHEN** Nonce received from Nuki during challenge-response
- **THEN** Check against circular buffer of size 8 (CONFIG_SDF_NUKI_NONCE_WINDOW)
- **THEN** If nonce in cache: reject with `SDF_NUKI_RESULT_ERR_NONCE_REUSE`
- **THEN** If not in cache: add to cache, evict oldest if full
- **THEN** Set Zigbee alarm bit 0x0008 (SECURITY_PROTOCOL) on replay detected

#### Scenario: Nonce persistence
- **WHEN** Nonce counter increments (per lock action sent)
- **THEN** Store counter to NVS ("sec_nonce_counter")
- **THEN** On boot: load counter, initialize cache with recent nonces

### Requirement: Encrypted NVS
The security policy SHALL require NVS encryption at all times.

#### Scenario: Encryption verification
- **WHEN** `sdf_storage_get_security_status()` called at boot
- **THEN** Verify `CONFIG_NVS_ENCRYPTION=y` and `nvs_keys` partition exists
- **THEN** If not encrypted: log critical error, set alarm bit 0x0008
- **THEN** Device SHALL NOT operate with unencrypted NVS

### Requirement: BLE Transport Security
The security policy SHALL mandate encryption for all Nuki BLE communication after pairing.

#### Scenario: Encrypted lock actions
- **WHEN** Paired state established
- **THEN** All lock action commands (cmd 0x0D) MUST be encrypted with libsodium secretbox
- **THEN** Authenticator MUST be included (HMAC-SHA256 of nonce+action+auth_id)
- **THEN** Unencrypted frames rejected

#### Scenario: Pairing encryption
- **WHEN** Pairing handshake
- **THEN** ECDH key exchange over Curve25519
- **THEN** Shared secret never transmitted
- **THEN** Authorization data encrypted with shared secret

### Requirement: Admin Authorization Required
The security policy SHALL require Admin fingerprint (permission == 3) for all configuration actions.

#### Scenario: Configuration actions requiring admin auth
- **WHEN** Action in {ENROLL, PAIR_NUKI, JOIN_ZIGBEE, FACTORY_RESET}
- **THEN** Must be authorized by fingerprint with permission == 3
- **THEN** Authorization timeout: 10 seconds
- **THEN** Non-admin fingerprint rejected with red LED flash

### Requirement: Zigbee Alarm Bits
The security policy SHALL define standard alarm bits for security events.

#### Scenario: Alarm bit definitions
- **WHEN** `0x0001` (ACTION_FAILURE): BLE lock action failed after retries
- **WHEN** `0x0002` (LOW_BATTERY): Battery < 20%
- **WHEN** `0x0004` (BIOMETRIC_LOCKOUT): 5 failed attempts in 60s
- **WHEN** `0x0008` (SECURITY_PROTOCOL): Nonce replay detected or NVS unencrypted

#### Scenario: Alarm reporting
- **WHEN** Alarm bit set
- **THEN** Update Zigbee attribute 0x0032 (AlarmMask)
- **WHEN** Alarm condition clears
- **THEN** Clear corresponding bit, send attribute report