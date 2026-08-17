# Tasks

## 1. New scratch module

- [x] 1.1 Create `firmware/components/sdf_ble_companion/include/sdf_ble_companion_gatt_scratch.h` exposing only `sdf_ble_companion_gatt_scratch_bind_owner()`, `_acquire()`, `_release()`, `_violation_count()`, and the buffer capacity constant — no array, no internal state
- [x] 1.2 Create `firmware/components/sdf_ble_companion/src/sdf_ble_companion_gatt_scratch.c` holding the `SDF_BLE_COMPANION_ATTR_MAX_LEN` static array, the owner `TaskHandle_t`, the held flag, and the violation counter
- [x] 1.3 Implement `bind_owner()` to record `xTaskGetCurrentTaskHandle()`; make re-binding the same task a silent no-op and re-binding a *different* task a logged violation that leaves the original owner in place
- [x] 1.4 Implement `acquire()` to return `NULL` when unbound, when already held, or when the caller is not the owner; increment the counter and `ESP_LOGE` with the calling task's name, marking it a contract violation rather than a client error
- [x] 1.5 Implement `release()` as idempotent — releasing when unheld is a no-op; releasing from a non-owner task is refused and counted, and must not clear the held flag
- [x] 1.6 Make the owner/held check atomic against other tasks with a short `portENTER_CRITICAL` region; do not rely on `s_lock`, which is not held across the whole acquire/release lifetime
- [x] 1.7 Add a test-only `sdf_ble_companion_gatt_scratch_reset_for_test()` guarded so it is not built for the device target, following the `sdf_event_router_reset_for_test()` precedent
- [x] 1.8 Add `src/sdf_ble_companion_gatt_scratch.c` to `firmware/components/sdf_ble_companion/CMakeLists.txt`

## 2. Bind the owner

- [x] 2.1 Call `sdf_ble_companion_gatt_scratch_bind_owner()` at the top of `sdf_ble_companion_on_host_sync()`, before `sdf_ble_companion_restart_advertising()` starts accepting connections
- [x] 2.2 Confirm no GATT access callback can run before that hook — advertising begins inside the same callback, so no client can connect earlier
- [x] 2.3 Confirm re-entry on NimBLE resync rebinds the same task handle and does not log a violation

## 3. Remove `auth_access` from shared staging

- [x] 3.1 Define `SDF_BLE_COMPANION_AUTH_WRITE_MAX_LEN` as `2 + (SDF_STORAGE_WEB_USER_NAME_MAX - 1) + SDF_STORAGE_WEB_USER_HASH_LEN` (65), derived from the constants rather than hardcoded, with a comment giving the per-command derivation
- [x] 3.2 Before changing the guard, re-verify each command's encoding against the code: `LOGIN_INIT` = 2 + username_len (`:304-307`), `LOGIN_VERIFY` = 1 + `SDF_SERVICES_WEB_AUTH_RESPONSE_LEN` (`:345`), `REGISTER` = 2 + username_len + `SDF_STORAGE_WEB_USER_HASH_LEN` (`:453-455`), `LOGOUT` = 1 (no length check at all today, `:484`)
- [x] 3.3 Tighten the write guard at `:299` from `len >= 2 && len < SDF_BLE_COMPANION_ATTR_MAX_LEN` to `len >= 1 && len <= SDF_BLE_COMPANION_AUTH_WRITE_MAX_LEN`, returning `BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN` and giving `s_lock` on rejection
- [x] 3.4 Replace `uint8_t *buf = s_gatt_scratch` (`:300`) with a `uint8_t buf[SDF_BLE_COMPANION_AUTH_WRITE_MAX_LEN]` stack local; leave every branch below it structurally unchanged
- [x] 3.5 Confirm no read of `buf` survives past `xSemaphoreGive(s_lock)` on any branch — `cmd` (`:302`), `username_len` (`:304`, `:452`), `conn->username` (`:312`), `response` (`:359`), `password_hash` (`:463`); the post-lock `on_auth_req` call (`:480`) must continue to receive `username_copy` and `password_hash`, not `buf`
- [x] 3.6 Confirm the per-command exact-length checks are still reachable and unchanged — the new cap is an outer bound, not a replacement for them
- [x] 3.7 Check that branches reading `buf[1]` (`:304`, `:452`) are unreachable at `len == 1`, now that the floor allows a 1-byte write

## 4. Pin the auth wire format

- [x] 4.1 Add an exact length check to the `LOGOUT` branch (`:484`): reject `len != 1` with `BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN` before mutating any connection state
- [x] 4.2 Confirm the relaxed floor's second-order effect is acceptable and intended: a 1-byte write with an unknown opcode now reaches the dispatch chain and returns `BLE_ATT_ERR_WRITE_NOT_PERMITTED` instead of failing the outer length guard
- [x] 4.3 Re-confirm no client in the tree sends `LOGOUT` — `web-companion/app.js:11` declares `SDF_AUTH_OPCODE_LOGOUT` without a send path, `tools/` and `scripts/` are empty
- [x] 4.4 Document the per-command wire format (opcode, layout, exact length) as a table in `doc/sdf_sas.md`, alongside the new component module — required by the Documentation Sync Rule for component structure, public API and runtime behavior
- [x] 4.5 Mirror the same per-command lengths as a comment on the opcode block in `web-companion/app.js:7-14`, replacing the now-stale "REGISTER and LOGOUT are unchanged" note; no send path changes
- [x] 4.6 If a LOGOUT send path is ever added to the client, it must be a 1-byte write — state this in the app.js comment so the constraint is recorded where it would be used

## 5. Collapse the remaining three staging sites into one

- [x] 5.1 Add a static staging helper in `sdf_ble_companion.c` that takes the mbuf, the length, the per-connection mirror buffer and length pointer, the connection handle, the callback ctx, and a dispatch function; it acquires staging, copies the payload, writes the mirror, gives `s_lock`, dispatches, releases staging, and returns an ATT code through a single exit
- [x] 5.2 Handle `acquire()` returning `NULL` inside the helper: give `s_lock` and return an ATT error without touching connection state or invoking the dispatch function
- [x] 5.3 Extract `config_access`'s post-lock body (`:610-636`) into a dispatch function preserving the admin-action parse, the `SDF_SERVICES_ADMIN_ACTION_NUKI_REPAIR` setup-state guard returning `BLE_ATT_ERR_WRITE_NOT_PERMITTED`, and the `on_config` passthrough
- [x] 5.4 Extract `enroll_access`'s post-lock body (`:685-688`) into a dispatch function — `on_enroll` passthrough, always returns 0
- [x] 5.5 Extract `ota_access`'s post-lock body (`:736-744`) into a dispatch function preserving the `false`-to-`BLE_ATT_ERR_UNLIKELY` mapping and its explanatory comment about the OTA wire format
- [x] 5.6 Route all three access callbacks through the helper; keep each one's own pre-staging checks (`s_initialized`, lock acquisition, `conn` lookup, auth-state gate, length guard) where they are
- [x] 5.7 Preserve the per-connection mirror semantics: `conn->config_value` / `enroll_value` / `ota_value` and their `_len` fields are still written before the lock is given, so the READ_CHR paths (`:671`, `:722`) are unaffected
- [x] 5.8 Confirm the mirror write happens before `xSemaphoreGive(s_lock)`, matching current ordering — it touches connection state that a concurrent disconnect memsets
- [x] 5.9 Delete `static uint8_t s_gatt_scratch[...]` and its comment block (`:104-117`); replace with a short pointer to the new module so the A14 rationale is not lost
- [x] 5.10 Verify `_acquire()` and `_release()` now appear exactly once each across the whole component

## 6. Tests

- [x] 6.1 Create `firmware/components/sdf_ble_companion/test/test_sdf_ble_companion_gatt_scratch.c`
- [x] 6.2 Register the new source and test in `firmware/test_runner/main/CMakeLists.txt` alongside the existing `bond_state` / `ota_protocol` entries
- [x] 6.3 Case: bind then acquire returns non-NULL; release; acquire again succeeds
- [x] 6.4 Case: acquire while unbound returns `NULL` and increments the counter
- [x] 6.5 Case: second acquire without release returns `NULL`, increments the counter, and leaves the first caller's bytes intact
- [x] 6.6 Case: acquire from a task other than the bound owner returns `NULL` and increments the counter — bind from a spawned FreeRTOS task, then acquire from the runner task
- [x] 6.7 Case: release when unheld is a no-op and does not increment the counter
- [x] 6.8 Case: release from a non-owner task is refused and leaves the buffer held by the owner
- [x] 6.9 Case: `bind_owner()` from a second task is refused and leaves the original owner effective

## 7. Verification

- [x] 7.1 Device build clean; `grep s_gatt_scratch` over `sdf_ble_companion.c` returns nothing
- [x] 7.2 Host runner (`build_linux`) green, including the new cases
- [x] 7.3 `esp-emu` boot: bind logged at host sync, no violation logs during a clean session. `I (806) sdf_ble_companion: Shared NimBLE host synced` immediately followed by `I (806) sdf_ble_scratch: GATT write staging owned by task 'nimble_host'`; no `sdf_ble_scratch` error lines and no panic over a 20 s boot. `bind_owner()` gained a one-shot `ESP_LOGI` on first bind so this is observable at all — it was silent on success as first written.
- [ ] 7.4 **Blocked on emulator capability, not on this change.** Reaching any GATT write requires an authenticated session, and the device cannot become connectable under `esp-emu`: it boots into sparse allow-list-filtered advertising (`BLE_HCI_ADV_FILT_CONN`) with an empty allow list (`Seeded BLE Companion allow list with 0 bonded peer(s)`, `Sparse, allow-list-filtered advertising started`), so no central can complete a connection until `sdf_ble_companion_open_pairing_window()` runs. That is reachable only from the physical-button admin flow (`sdf_app.c:514`) — no CLI command opens it — and the device is UNCLAIMED, so admin enrollment must come first, which needs the fingerprint sensor the emulator does not model (`Sensor probe FAILED after 3 attempts`). Registering a web user has the same dependency: `on_auth_request` routes to `SDF_SERVICES_ADMIN_ACTION_WEB_REG_AUTH`, gated on an admin fingerprint. Bumble 0.0.233 is installed and `esp-emu --ble-hci tcp:` is available, so the missing piece is a fingerprint/GPIO simulation harness — new infrastructure, out of scope here. A prior change already parked `esp-emu --ble-hci` as an open follow-on (`archive/2026-08-15-fix-ble-bond-seed-init-order/tasks.md` 6.1).
- [ ] 7.5 Blocked — same reason as 7.4.
- [ ] 7.6 Blocked — same reason as 7.4.
- [ ] 7.7 Blocked — same reason as 7.4.
- [x] 7.8 Cross-checked command by command against the enforced checks: outer bound `len >= 1 && len <= 65` (`:327`), LOGIN_INIT `len != 2 + username_len` with `1 <= username_len <= 31` (`:333-335`), LOGIN_VERIFY `len != 1 + 32` (`:373`), REGISTER `len != 2 + username_len + 32` with the same username bounds (`:481-483`), LOGOUT `len != 1` (`:516`), unknown opcode → `BLE_ATT_ERR_WRITE_NOT_PERMITTED`. The `doc/sdf_sas.md` table and the `web-companion/app.js` comment agree on every row.
- [ ] 7.9 Blocked — same reason as 7.4.
- [ ] 7.10 Blocked — same reason as 7.4.
- [ ] 7.11 Partially verified: no `sdf_ble_scratch` violation lines over a clean 20 s boot, but "a full clean session" means one carrying GATT traffic, which 7.4 is blocked on.
- [x] 7.12 Measured from the linked image rather than estimated. Frame sizes: `auth_access` 352 B (includes the new 65-byte `buf`), `config_access`/`enroll_access`/`ota_access` 32 B each, `stage_write` 96 B, `dispatch_config_write` 48 B, `dispatch_enroll_write`/`dispatch_ota_write` 16 B. Deepest staged-write path is 32 + 96 + 48 = 176 B. No frame anywhere on these paths is near the 512 B that made A14 a finding, against a 4096 B `CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE`.
- [x] 7.13 No emulator panic occurred, so nothing to dismiss. The three `Panic intercept:` lines in the log are esp-emu registering symbol interception at load, not panics.
- [x] 7.14 Run `openspec validate --strict guard-ble-gatt-scratch-ownership`
