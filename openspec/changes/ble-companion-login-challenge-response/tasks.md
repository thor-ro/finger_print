## 1. Storage: salted/stretched credential shape

- [ ] 1.1 Change `sdf_storage_web_user_t` (`sdf_storage.h`) from `password_hash[32]` to `salt[16]` + `stretched_credential[32]`; bump any on-disk versioning/format the storage layer relies on
- [ ] 1.2 Update `sdf_storage_web_user_save`/`sdf_storage_web_user_load`/`sdf_storage_web_user_find_by_name` and their tests (`test_sdf_storage.c`) for the new struct shape
- [ ] 1.3 Add device-local pseudo-salt HMAC key storage (generate once via `esp_fill_random`, persist through `sdf_storage`, load-or-generate-on-first-use) and cover it with a storage test
- [ ] 1.4 Confirm `sdf_storage_erase_all` clears the pseudo-salt key together with web user accounts, so the two can never drift out of sync

## 2. Crypto/decision-logic primitives (pure functions, `sdf_services_web_auth.c`)

- [ ] 2.1 Add a PBKDF2-HMAC-SHA256 stretch function (`mbedtls_pkcs5_pbkdf2_hmac`) taking a received hash + salt + iteration count, returning the stretched credential
- [ ] 2.2 Update `sdf_services_web_auth_decide_registration` to generate a random salt and produce a `stretched_credential` (via 2.1) instead of persisting the raw received hash; update its tests
- [ ] 2.3 Replace `sdf_services_web_auth_verify_login` with a challenge/response decision function: given a stored `stretched_credential`, an outstanding nonce, and a submitted response, return match/no-match via `mbedtls_ct_memcmp`; update its tests (matching, mismatched, wrong-length, all-zero cases carried over from the old tests)
- [ ] 2.4 Add a decision function producing the `LOGIN_INIT` challenge fields for a known user (stored salt, compile-time iteration count, fresh nonce) and, separately, the deterministic pseudo-salt path for an unknown user (`HMAC(pseudo_salt_key, username)`) so both are unit-testable without BLE/storage I/O
- [ ] 2.5 Unit test that known-user and unknown-user challenge responses are the same shape/length (enumeration-indistinguishability property)

## 3. BLE wire protocol (`sdf_ble_companion.c`)

- [ ] 3.1 Define new opcodes for `LOGIN_INIT` and `LOGIN_VERIFY`, replacing the old single-message `LOGIN` opcode's handling (keep `REGISTER` and `LOGOUT` opcodes unchanged)
- [ ] 3.2 Add per-connection ephemeral state to `sdf_ble_companion_connection_t` for the outstanding nonce and challenge-issued flag/state, alongside where `password_hash` lives today
- [ ] 3.3 Implement `LOGIN_INIT` write handling: look up user (or fall back to the deterministic pseudo-salt path for unknown users), generate a nonce, store it in the connection, transition `conn->auth_state` to reflect a pending challenge
- [ ] 3.4 Extend the Auth characteristic's read branch to return `{salt, iteration_count, nonce}` when a challenge is pending for that connection, instead of only `"AUTH_OK"`/`"AUTH_REQUIRED"`
- [ ] 3.5 Implement `LOGIN_VERIFY` write handling: compute expected response from the stored `stretched_credential` and outstanding nonce, compare via the function from 2.3, authenticate on match, and invalidate the nonce afterward either way (single-use)
- [ ] 3.6 Reject a `LOGIN_VERIFY` with no outstanding nonce for that connection (e.g. replay of an already-consumed nonce, or `LOGIN_VERIFY` sent without a prior `LOGIN_INIT`)
- [ ] 3.7 Wire the existing failed-login lockout counter (`sdf_ble_companion_bond_note_login_failure`/`note_login_success`/eviction) to `LOGIN_VERIFY` outcomes only; confirm `LOGIN_INIT` does not touch the counter
- [ ] 3.8 Clear per-connection nonce/challenge state on disconnect (existing `memset` path) in addition to post-verify clearing

## 4. Firmware tests

- [ ] 4.1 Add/port BLE-level tests (or extend bond-state tests) covering: challenge issued for known user, indistinguishable challenge for unknown user, successful verify, failed verify does not authenticate, nonce replay rejected, `LOGIN_INIT` doesn't move the failed-login counter, threshold reached still evicts via `LOGIN_VERIFY` failures
- [ ] 4.2 Run the full firmware test suite (`sdf_services`, `sdf_storage`, `sdf_ble_companion`) and fix any fallout from the struct/API shape changes

## 5. Web Companion App (`web-companion/app.js`)

- [ ] 5.1 Implement the `LOGIN_INIT` write + Auth characteristic read to fetch `{salt, iteration_count, nonce}`
- [ ] 5.2 Implement client-side PBKDF2 stretch via `crypto.subtle.deriveBits`, and the `HMAC-SHA256(stretched_credential, nonce)` response computation
- [ ] 5.3 Implement `LOGIN_VERIFY` write and success/failure handling in the login UI flow
- [ ] 5.4 Leave `REGISTER` flow unchanged (still sends `SHA256(password)`); verify it still round-trips against the updated firmware
- [ ] 5.5 Surface a distinct "please update the app" style error when the device rejects the old LOGIN opcode format, rather than a generic auth-failure message (see design.md Risks)

## 6. Calibration

- [ ] 6.1 Benchmark `mbedtls_pkcs5_pbkdf2_hmac` on-device at the OWASP-baseline iteration count (~210k) to confirm one-time REGISTER-side cost is acceptable; adjust the compile-time iteration constant if not
- [ ] 6.2 Pin final salt/nonce/response byte lengths in the shared header(s) used by both the wire-protocol code and its tests
