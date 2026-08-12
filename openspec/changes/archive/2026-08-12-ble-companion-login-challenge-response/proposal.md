## Why

The BLE Companion Service's LOGIN characteristic stores and compares a raw, unsalted `SHA256(password)` computed by the browser. Two distinct weaknesses follow from that: (1) a stolen copy of the stored hash (e.g. via physical flash extraction) can be cracked offline at GPU speed, with no per-user salt to defeat rainbow tables; and (2) the value the browser sends over the wire on every LOGIN *is* the credential — anyone who ever captures it can replay it indefinitely without knowing the password or cracking anything. The predecessor change (`ble-companion-trust-and-lockout`) explicitly named both problems and deferred them as "a separate follow-on" — this is that follow-on.

## What Changes

- **BREAKING**: Replace the single-message LOGIN (`[cmd][username][password_hash]`) with a two-round-trip challenge-response exchange: `LOGIN_INIT` (client sends username) → server replies with `{salt, iteration_count, nonce}` → client sends `LOGIN_VERIFY` with `HMAC-SHA256(stretched_credential, nonce)`.
- Move password stretching to the browser: at REGISTER time the server still receives `SHA256(password)` as today, but now generates a random per-user salt and runs PBKDF2-HMAC-SHA256 (via mbedtls `pkcs5.h`, already vendored in the ESP-IDF toolchain) once to produce the `stretched_credential` that gets persisted — never the raw hash. At LOGIN time, the browser (not the device) performs the same PBKDF2 stretch using Web Crypto, keeping the device's per-attempt cost down to a single HMAC comparison so the failed-login lockout's attempt budget stays cheap to enforce.
- Grow `sdf_storage_web_user_t`: replace `password_hash[32]` with `salt[16]` + `stretched_credential[32]`.
- Add a per-connection ephemeral nonce (issued on `LOGIN_INIT`, single-use, cleared on `LOGIN_VERIFY` or disconnect) so a captured verify response can never be replayed against a future login.
- Preserve the existing no-username-enumeration property: `LOGIN_INIT` for an unknown username returns a deterministic pseudo-salt (`HMAC(device_secret, username)`) instead of an error, so the response shape/timing is indistinguishable from a real account.
- Wire the existing failed-login lockout counter (`bond_note_login_failure`/eviction) to `LOGIN_VERIFY` failures only; `LOGIN_INIT` does not count as an attempt.
- `web-companion/app.js` gains the two-step LOGIN flow and a PBKDF2 derivation step via `crypto.subtle.deriveBits`; `REGISTER` stays wire-unchanged (still sends `SHA256(password)` once).

Out of scope: REGISTER's one-time exposure of `SHA256(password)` to the device before it's salted is an accepted, pre-existing residual risk (REGISTER already requires admin-fingerprint + physical presence, same trust boundary the rest of this subsystem relies on) and is not addressed here.

## Capabilities

### New Capabilities
(none)

### Modified Capabilities
- `ble-companion-service`: "BLE GATT Authentication" requirement changes from a single-message hash comparison to a challenge-response exchange; "Failed BLE Login Lockout With Bond Eviction" requirement's trigger condition changes from "password hash mismatch" to "verify response mismatch".
- `web-companion-app`: new "User Login" requirement describing the two-step challenge-response client flow and client-side PBKDF2 stretching; "User Registration" requirement's scenario text is clarified to note the server now salts/stretches what it receives (wire behavior unchanged).

## Impact

- `firmware/components/sdf_ble_companion/src/sdf_ble_companion.c`: LOGIN opcode handling replaced with `LOGIN_INIT`/`LOGIN_VERIFY`; new ephemeral per-connection nonce field.
- `firmware/components/sdf_services/src/sdf_services_web_auth.c`: `sdf_services_web_auth_verify_login` replaced with challenge/verify decision functions; registration decision function gains salt generation + PBKDF2 stretch.
- `firmware/components/sdf_storage/include/sdf_storage.h` (+ implementation): `sdf_storage_web_user_t` shape change (`password_hash` → `salt` + `stretched_credential`).
- `web-companion/app.js`: login flow rewritten to the two-step exchange with client-side PBKDF2 via Web Crypto.
- Greenfield deployment (no companion devices bonded in the field yet, per `ble-companion-trust-and-lockout`), so no migration path is needed — this is a clean breaking wire-protocol change.
