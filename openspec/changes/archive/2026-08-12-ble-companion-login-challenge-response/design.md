## Context

See proposal.md - Why. Relevant current-state facts that shape the approach:

- LOGIN today is a single synchronous GATT write: `[cmd][username][SHA256(password)]`, checked with `mbedtls_ct_memcmp` inside the same write callback, and the ATT write-response status (`0` or `BLE_ATT_ERR_INSUFFICIENT_AUTHEN`) directly communicates success/failure - no read or notify round trip is needed today. That synchronous shape is what has to change: a BLE ATT Write Response carries a status code only, never a data payload, so a server-generated challenge (salt/iteration-count/nonce) cannot be returned from the `LOGIN_INIT` write itself.
- The Auth characteristic already supports a client-initiated read (`ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR`), currently returning the literal string `"AUTH_OK"` or `"AUTH_REQUIRED"` depending on `conn->auth_state`.
- The Auth characteristic also already supports server-pushed notifications (`conn->auth_value`/`auth_value_len`, `sdf_ble_companion_set_authenticated`), used today to deliver the REGISTER admin-fingerprint pending-action result asynchronously (bounded only by the pending-action timeout, since it waits on a human scanning a finger). The Web App already subscribes to Auth notifications on connect (`authChar.startNotifications()` in `app.js`).
- `mbedtls/pkcs5.h` (PBKDF2) and `mbedtls/constant_time.h` are already vendored in the ESP-IDF toolchain this project builds against; no new firmware dependency is needed.
- `mbedtls_ct_memcmp` is the existing constant-time comparison primitive used for the current hash check; the same primitive is reused for comparing the `LOGIN_VERIFY` response.
- No device-unique secret currently exists anywhere in this codebase (searched `sdf_ble_companion`, `sdf_services`, `sdf_storage` for `device_secret`/`hmac_key`/efuse-derived key material - none found). The username-enumeration mitigation in the proposal needs one, and it doesn't yet exist.
- Deployment topology: firmware ships via signed BLE OTA; `web-companion/` is static assets intended for GitHub Pages, deployed independently of firmware. A device and a browser tab are not guaranteed to update in lockstep.

## Goals / Non-Goals

**Goals:**
- Make the value that crosses the wire on every LOGIN cryptographically bound to a single-use, server-issued nonce, so a captured LOGIN transcript is not replayable.
- Store only a salted, KDF-stretched credential at rest, never a raw password hash, so a flash/NVS extraction requires offline cracking work proportional to the KDF cost rather than a raw SHA-256 lookup.
- Keep the ESP32's per-LOGIN-attempt cost to a single HMAC comparison, so the failed-login lockout (`ble-companion-trust-and-lockout`) continues to bound an attacker to cheap, fast rejections rather than the device itself becoming the bottleneck.
- Preserve the existing no-username-enumeration property of `verify_login` (today, "user not found" and "wrong hash" are indistinguishable).

**Non-Goals:**
- Protecting the one-time `SHA256(password)` REGISTER submission itself - that message is still sent in the clear form the server salts *after* receipt, same as today, and remains covered only by BLE link encryption plus the existing admin-fingerprint/physical-presence gate on REGISTER.
- Runtime-configurable KDF iteration count or nonce/salt sizes. Like the trust-and-lockout thresholds, these are structural safety parameters for a BLE-reachable subsystem and stay compile-time constants, not values settable over any BLE-reachable configuration surface.
- Any change to REGISTER's wire shape, admin-fingerprint gating, or the pending-admin-action mechanism.
- A protocol-version negotiation mechanism between `web-companion` and firmware beyond what's noted under Risks below.

## Decisions

**Challenge delivery: write-then-read, not write-then-notify.**
`LOGIN_INIT` is fully synchronous (a user lookup plus a random nonce generation - no human interaction to wait on), unlike REGISTER's admin-fingerprint gate which genuinely needs the async notify path because it blocks on a physical scan. Extending the Auth characteristic's existing read branch - which already conditionally returns different content based on `conn->auth_state` (`"AUTH_OK"`/`"AUTH_REQUIRED"`) - to also return challenge bytes when a challenge is pending for that connection reuses that same mechanism rather than introducing notify-subscription-lifecycle concerns (was the client actually subscribed yet? did a notification get dropped?) for a value that's available immediately. Flow: client writes `LOGIN_INIT` -> write succeeds -> client reads Auth characteristic -> server returns `{salt, iteration_count, nonce}` while `conn->auth_state == CHALLENGE_ISSUED`. REGISTER's async notify path is unchanged and coexists on the same characteristic without conflict, since it's driven by a different op (`AUTH_REGISTER`) and different state.

**Nonce and pending-challenge state live in the per-connection struct, not the bond-tracking state.**
Unlike the failed-login counter (which must survive disconnect/reconnect to be a meaningful lockout), a LOGIN nonce is inherently single-connection, single-attempt scoped. It belongs next to where `password_hash` lives today in `sdf_ble_companion_connection_t`, cleared by the same `memset` that already runs on disconnect and by an explicit clear immediately after one `LOGIN_VERIFY` attempt (success or failure) - not by a lock-free global table, and not persisted.

**Username-enumeration mitigation: a device-local pseudo-salt HMAC key, generated once and stored via `sdf_storage`.**
No such secret exists yet. Rather than deriving it from something already-readable like the BLE MAC address (fine against a remote observer, but a needless assumption to lean on) or introducing a new hardware-unique-ID dependency, generate a random key once (`esp_fill_random`) the first time it's needed and persist it through `sdf_storage`, matching the existing pattern for other device-local secrets (e.g. the Nuki shared key via `sdf_storage_nuki_save`) and inheriting the same NVS-encryption protection already in place for that data. `LOGIN_INIT` for an unknown username derives a deterministic pseudo-salt as `HMAC(pseudo_salt_key, username)`, so responses are shaped identically whether or not the account exists, and no `LOGIN_VERIFY` can ever succeed against it.

**KDF: PBKDF2-HMAC-SHA256, run by the browser at LOGIN, run once by the device at REGISTER.**
Chosen over Argon2/scrypt because `mbedtls/pkcs5.h` (PBKDF2) is already vendored for this toolchain with no new dependency, and Web Crypto's `deriveBits` supports PBKDF2 natively with no library needed in `app.js` either - both sides get the same algorithm for free. A memory-hard KDF (Argon2/scrypt) would be a stronger choice against GPU/ASIC cracking, but neither side currently has a vetted, dependency-free implementation available, and introducing one is a larger and separable change; PBKDF2 with a compile-time iteration count still closes the "raw SHA-256, no stretching at all" gap this change targets. Revisit if a vetted memory-hard option becomes cheap to add later.
Iteration count is a placeholder pending a quick on-device benchmark (see Open Questions) - target the current OWASP PBKDF2-HMAC-SHA256 baseline (~210k iterations) if the one-time REGISTER-side cost proves acceptable, scaling down only if it doesn't.

**REGISTER keeps its current wire shape; the KDF stretch happens server-side, after receipt, once.**
The client still sends `SHA256(password)` exactly as today - REGISTER is rare, already admin-fingerprint-gated, and has no rate-limit pressure, so paying the one-time PBKDF2 cost on the ESP32 at REGISTER (rather than requiring the client to already know salt/iteration-count parameters before an account exists) keeps REGISTER's wire format and client code path unchanged.

## Risks / Trade-offs

- **[Risk]** `web-companion` (GitHub Pages) and firmware (signed BLE OTA) deploy independently, so a browser tab running old `app.js` (single-message LOGIN) can connect to updated firmware (challenge-response only), or vice versa, after this ships. Old `app.js` writing the old LOGIN opcode format to new firmware will simply hit `BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN` or an unrecognized-opcode rejection today's malformed-write handling already covers, rather than silently authenticating incorrectly. -> **Mitigation**: accept this as a hard cutover (greenfield deployment, no companion devices bonded yet, per `ble-companion-trust-and-lockout`) and make sure the failure surfaces a clear "update the app" message in the UI rather than a generic auth error; a full version-negotiation handshake is out of scope (see Non-Goals).
- **[Risk]** PBKDF2 (not memory-hard) is weaker against GPU/ASIC offline cracking than Argon2/scrypt would be. -> **Mitigation**: accepted for this change given no dependency-free memory-hard KDF is currently available on either side (browser or ESP32 toolchain); still a large improvement over the current unsalted, unstretched SHA-256. Revisit if that changes.
- **[Risk]** The new device-local pseudo-salt HMAC key is a single point of failure for the enumeration mitigation - if it's ever regenerated (e.g. accidentally, or on factory reset without matching account wipe) or leaked, unknown-username responses could become distinguishable, or (if leaked) forgeable. -> **Mitigation**: generate once, store through the same NVS-encrypted path as other device secrets; factory reset (`sdf_storage_erase_all`) already clears all web user accounts together, so key and accounts stay consistent as a unit.
- **[Risk]** Moving the KDF stretch to the browser trusts the browser's Web Crypto PBKDF2 implementation and the client-supplied response's *shape* is still just a value the device compares - a modified/malicious client could in principle skip deriving from the real password and instead directly compute the response if it already possesses the stored `stretched_credential` (e.g. from a prior flash extraction). This is expected and unavoidable: a stolen `stretched_credential` is exactly as fatal as a stolen password in any HMAC-based scheme. What this change actually removes is the ability to replay a *captured wire transcript* without ever obtaining the stretched credential itself, and the ability to crack an extracted one cheaply. -> **Mitigation**: none needed beyond stating the boundary clearly; this matches the threat model of every challenge-response scheme.

## Migration Plan

None required in the data sense: greenfield deployment, no companion devices bonded in the field yet (per `ble-companion-trust-and-lockout`), so there's no existing `password_hash` data to convert. Deployment sequencing is still worth calling out: ship firmware and `web-companion` together (or firmware first, app second, accepting a brief "old app can't log in" window) rather than the reverse, since the Risk above means an old app talking to new firmware fails closed, not open.

## Open Questions

- Exact PBKDF2 iteration count: needs a quick on-device benchmark of `mbedtls_pkcs5_pbkdf2_hmac` at the OWASP-baseline iteration count to confirm the one-time REGISTER-side cost is acceptable alongside concurrent BLE/Zigbee/Nuki work, before pinning the compile-time constant. Doesn't change the approach or the wire protocol shape either way, only the constant's value.
- Exact nonce/salt byte lengths (proposal sketches salt=16B, nonce likely 16B, response=32B/HMAC-SHA256 output) - fine to finalize during implementation, doesn't affect the design's shape.
