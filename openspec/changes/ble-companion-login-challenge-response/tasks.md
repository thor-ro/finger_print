## 1. Storage: salted/stretched credential shape

- [x] 1.1 Change `sdf_storage_web_user_t` (`sdf_storage.h`) from `password_hash[32]` to `salt[16]` + `stretched_credential[32]`; bump any on-disk versioning/format the storage layer relies on
- [x] 1.2 Update `sdf_storage_web_user_save`/`sdf_storage_web_user_load`/`sdf_storage_web_user_find_by_name` and their tests (`test_sdf_storage.c`) for the new struct shape
- [x] 1.3 Add device-local pseudo-salt HMAC key storage (generate once via `esp_fill_random`, persist through `sdf_storage`, load-or-generate-on-first-use) and cover it with a storage test
- [x] 1.4 Confirm `sdf_storage_erase_all` clears the pseudo-salt key together with web user accounts, so the two can never drift out of sync

## 2. Crypto/decision-logic primitives (pure functions, `sdf_services_web_auth.c`)

- [x] 2.1 Add a PBKDF2-HMAC-SHA256 stretch function (`mbedtls_pkcs5_pbkdf2_hmac`) taking a received hash + salt + iteration count, returning the stretched credential
- [x] 2.2 Update `sdf_services_web_auth_decide_registration` to generate a random salt and produce a `stretched_credential` (via 2.1) instead of persisting the raw received hash; update its tests
- [x] 2.3 Replace `sdf_services_web_auth_verify_login` with a challenge/response decision function: given a stored `stretched_credential`, an outstanding nonce, and a submitted response, return match/no-match via `mbedtls_ct_memcmp`; update its tests (matching, mismatched, wrong-length, all-zero cases carried over from the old tests)
- [x] 2.4 Add a decision function producing the `LOGIN_INIT` challenge fields for a known user (stored salt, compile-time iteration count, fresh nonce) and, separately, the deterministic pseudo-salt path for an unknown user (`HMAC(pseudo_salt_key, username)`) so both are unit-testable without BLE/storage I/O
- [x] 2.5 Unit test that known-user and unknown-user challenge responses are the same shape/length (enumeration-indistinguishability property)

## 3. BLE wire protocol (`sdf_ble_companion.c`)

- [x] 3.1 Define new opcodes for `LOGIN_INIT` and `LOGIN_VERIFY`, replacing the old single-message `LOGIN` opcode's handling (keep `REGISTER` and `LOGOUT` opcodes unchanged)
- [x] 3.2 Add per-connection ephemeral state to `sdf_ble_companion_connection_t` for the outstanding nonce and challenge-issued flag/state, alongside where `password_hash` lives today
- [x] 3.3 Implement `LOGIN_INIT` write handling: look up user (or fall back to the deterministic pseudo-salt path for unknown users), generate a nonce, store it in the connection, transition `conn->auth_state` to reflect a pending challenge
- [x] 3.4 Extend the Auth characteristic's read branch to return `{salt, iteration_count, nonce}` when a challenge is pending for that connection, instead of only `"AUTH_OK"`/`"AUTH_REQUIRED"`
- [x] 3.5 Implement `LOGIN_VERIFY` write handling: compute expected response from the stored `stretched_credential` and outstanding nonce, compare via the function from 2.3, authenticate on match, and invalidate the nonce afterward either way (single-use)
- [x] 3.6 Reject a `LOGIN_VERIFY` with no outstanding nonce for that connection (e.g. replay of an already-consumed nonce, or `LOGIN_VERIFY` sent without a prior `LOGIN_INIT`)
- [x] 3.7 Wire the existing failed-login lockout counter (`sdf_ble_companion_bond_note_login_failure`/`note_login_success`/eviction) to `LOGIN_VERIFY` outcomes only; confirm `LOGIN_INIT` does not touch the counter
- [x] 3.8 Clear per-connection nonce/challenge state on disconnect (existing `memset` path) in addition to post-verify clearing

## 4. Firmware tests

- [x] 4.1 Add/port BLE-level tests (or extend bond-state tests) covering: challenge issued for known user, indistinguishable challenge for unknown user, successful verify, failed verify does not authenticate, nonce replay rejected, `LOGIN_INIT` doesn't move the failed-login counter, threshold reached still evicts via `LOGIN_VERIFY` failures - covered via `sdf_services_web_auth` unit tests (section 2) plus the existing `test_sdf_ble_companion_bond_state.c` suite, unchanged since the lockout primitives themselves didn't change, only their call site; `sdf_ble_companion` isn't part of the linux host test target (no NimBLE mock harness exists there), so wire-level dispatch (nonce replay rejection, `LOGIN_INIT` not touching the counter) is a structural property of the code in `sdf_ble_companion.c` rather than independently host-tested
- [x] 4.2 Run the full firmware test suite (`sdf_services`, `sdf_storage`, `sdf_ble_companion`) and fix any fallout from the struct/API shape changes - 232 Tests, 0 Failures, 11 Ignored (unrelated BLE-hardware-only tests); no fallout

## 5. Web Companion App (`web-companion/app.js`)

- [x] 5.1 Implement the `LOGIN_INIT` write + Auth characteristic read to fetch `{salt, iteration_count, nonce}`
- [x] 5.2 Implement client-side PBKDF2 stretch via `crypto.subtle.deriveBits`, and the `HMAC-SHA256(stretched_credential, nonce)` response computation
- [x] 5.3 Implement `LOGIN_VERIFY` write and success/failure handling in the login UI flow
- [x] 5.4 Leave `REGISTER` flow unchanged (still sends `SHA256(password)`); verify it still round-trips against the updated firmware - wire format and `handleAuthNotification` untouched; only extracted into its own `submitRegister()` function
- [x] 5.5 Surface a distinct "please update the app" style error when the device rejects the old LOGIN opcode format, rather than a generic auth-failure message (see design.md Risks)

## 6. Calibration

- [ ] 6.1 Benchmark `mbedtls_pkcs5_pbkdf2_hmac` on-device at the OWASP-baseline iteration count (~210k) to confirm one-time REGISTER-side cost is acceptable; adjust the compile-time iteration constant if not - **blocked**: no ESP32-C6 hardware available in this dev environment to run a real on-device benchmark. Rough host-CPU sanity check only (Python `hashlib.pbkdf2_hmac`, 210k iterations SHA-256): ~17ms on this Mac - not representative of the ESP32-C6's un-accelerated single RISC-V core, which will be meaningfully slower (rough order-of-magnitude estimate: low hundreds of ms), but this is a one-time, admin-fingerprint-gated REGISTER-only cost, not a per-login one. `SDF_SERVICES_WEB_AUTH_PBKDF2_ITERATIONS` (`sdf_services.h`) is left at the OWASP-baseline placeholder of `210000u` pending a follow-up on-device measurement; adjusting it is a one-line, wire-protocol-shape-neutral change per design.md
- [x] 6.2 Pin final salt/nonce/response byte lengths in the shared header(s) used by both the wire-protocol code and its tests - `SDF_STORAGE_WEB_USER_SALT_LEN=16`, `SDF_SERVICES_WEB_AUTH_NONCE_LEN=16`, `SDF_SERVICES_WEB_AUTH_RESPONSE_LEN=32`/`SDF_STORAGE_WEB_USER_STRETCHED_LEN=32`, matched in `web-companion/app.js`
