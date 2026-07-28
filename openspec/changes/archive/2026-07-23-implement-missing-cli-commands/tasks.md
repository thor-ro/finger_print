# Tasks: Implement Missing CLI Commands

## Phase 1: User Management Commands

### 1.1 Implement `user list`
- [ ] Add `cmd_user_list()` handler in `sdf_cli_commands.c`
- [ ] Call `sdf_services_query_users()` with max buffer (4096 entries)
- [ ] Format table output: "User ID  Permission" with permission names (Standard/Elevated/Admin)
- [ ] Add argtable3 entry for `user list`
- [ ] Add Unity test: `test_user_list_formats_output`

### 1.2 Implement `user get <id>`
- [ ] Add `cmd_user_get()` handler
- [ ] Parse user_id arg (validate 1-4095)
- [ ] Call `fp_query_user_permission(user_id, &perm)`
- [ ] Print: "User ID: <id>, Permission: <perm> (<name>)"
- [ ] Add Unity test: `test_user_get_valid_id`, `test_user_get_invalid_id`

### 1.3 Implement `user add <id> <perm>`
- [ ] Add `cmd_user_add()` handler
- [ ] Parse user_id (1-4095) and permission (1-3)
- [ ] Check user_id not occupied via `sdf_services_query_users()`
- [ ] Call `sdf_services_request_enrollment(user_id, permission)`
- [ ] Run 3-step enrollment loop:
  - Prompt "Scan finger (step N of 3)..."
  - Call `fp_enroll_step(step, user_id, permission)`
  - On ACK_FAIL step 1-2: retry same step
  - On ACK_FAIL step 3: fail enrollment
  - On ACK_OK: continue
- [ ] Handle timeout (fp_enroll_step returns TIMEOUT)
- [ ] Print success/failure with LED feedback hints
- [ ] Add Unity tests for success, user occupied, timeout paths

### 1.4 Implement `user del <id>`
- [ ] Add `cmd_user_del()` handler
- [ ] Parse user_id, validate
- [ ] Call `sdf_services_delete_user(user_id)`
- [ ] Handle errors: not found, invalid state
- [ ] Add Unity test

### 1.5 Implement `user permission <id> <perm>` (already exists — verify)
- [ ] Verify existing `cmd_user` handles "permission" subcommand correctly
- [ ] Ensure it calls `sdf_services_change_user_permission()`
- [ ] Add Unity test if missing

## Phase 2: Nuki Management Commands

### 2.1 Implement `nuki status`
- [ ] Add `cmd_nuki_status()` handler
- [ ] Check credentials via `sdf_storage_nuki_load()`
- [ ] Check BLE transport via `sdf_nuki_ble_is_ready()`
- [ ] Print:
  ```
  Paired: yes/no
  Authorization ID: 0xXXXXXXXX / N/A
  BLE Transport: ready/disconnected
  Last Keyturner State: locked/unlocked/unknown
  ```
- [ ] Add Unity test with mocked storage + BLE

### 2.2 Implement `nuki connect`
- [ ] Add `cmd_nuki_connect()` handler
- [ ] Call `sdf_nuki_ble_set_enabled(&s_ble, true)`
- [ ] Call `sdf_nuki_ble_start(&s_ble)`
- [ ] Poll `sdf_nuki_ble_is_ready(&s_ble)` with timeout (10s)
- [ ] Print connection status
- [ ] Add Unity test

### 2.3 Implement `nuki pair`
- [ ] Add `cmd_nuki_pair()` handler
- [ ] Check if already paired (warn but allow re-pair)
- [ ] Enable BLE transport
- [ ] Initialize pairing: `sdf_nuki_pairing_init(&s_pairing, &s_client, 1, SDF_APP_ID, SDF_APP_NAME)`
- [ ] Start pairing: `sdf_nuki_pairing_start(&s_pairing)`
- [ ] Wait for completion (poll `s_pairing.state` or use callback if available):
  - Timeout: 60s
  - On `SDF_NUKI_PAIRING_COMPLETE`: get credentials, save via `sdf_storage_nuki_save()`
  - On error: print error code
- [ ] Print authorization ID on success
- [ ] Add Unity test with mocked pairing flow

### 2.4 Implement `nuki unpair`
- [ ] Add `cmd_nuki_unpair()` handler
- [ ] Call `sdf_nuki_ble_stop(&s_ble)`
- [ ] Clear credentials: `sdf_storage_nuki_clear()`
- [ ] Clear BLE target: `sdf_storage_ble_target_clear()`
- [ ] Print confirmation
- [ ] Add Unity test

## Phase 3: Zigbee Management Commands

### 3.1 Implement `zigbee status`
- [ ] Add `cmd_zigbee_status()` handler
- [ ] Check `sdf_protocol_zigbee_is_ready()`
- [ ] If ready, query ESP Zigbee APIs for:
  - PAN ID, Extended PAN ID, Channel, Short Address
  - Network state (joined/not joined)
- [ ] Print formatted status
- [ ] Add Unity test

### 3.2 Implement `zigbee connect` (permit join)
- [ ] Add `cmd_zigbee_connect()` handler
- [ ] Call `sdf_protocol_zigbee_permit_join()`
- [ ] Print "Network steering enabled (join window open)"
- [ ] Add Unity test

### 3.3 Implement `zigbee unpair` (factory reset)
- [ ] Add `cmd_zigbee_unpair()` handler
- [ ] Call `sdf_protocol_zigbee_factory_reset()`
- [ ] Print "Zigbee network left, NVRAM cleared"
- [ ] Add Unity test

## Phase 4: Refactoring & Polish

### 4.1 Code Organization
- [ ] Extract command handlers into separate static functions for readability
- [ ] Add consistent error message formatting
- [ ] Ensure all commands reset CLI idle timer via `check_auth()` → `sdf_cli_authenticate()`

### 4.2 Help Text
- [ ] Update each command's `.help` and `.hint` in `esp_console_cmd_t` registration
- [ ] Ensure `user`, `nuki`, `zigbee` top-level help shows subcommands

### 4.3 Test Infrastructure
- [ ] Add mock implementations for `sdf_services`, `fingerprint`, `sdf_protocol_ble`, `sdf_protocol_zigbee` in test file or mock headers
- [ ] Run test_runner build to verify compilation

## Phase 5: Documentation

### 5.1 Update User Manual
- [ ] Add "USB-C CLI Commands" section to `doc/user_manual.md` with command reference table
- [ ] Include auth requirement notes (CLI login + admin FP for mutating commands)

## Phase 6: Verification

### 6.1 Build & Test
- [ ] `cd firmware && idf.py build` — verify no compile errors
- [ ] `cd firmware/test_runner && idf.py build` — verify tests compile
- [ ] Flash test_runner to hardware, run Unity tests
- [ ] Manual CLI test via USB-C: verify each command works end-to-end

---

## Dependencies Between Tasks

```
Phase 1 (User) ──▶ Phase 2 (Nuki) ──▶ Phase 3 (Zigbee)
     │                  │                   │
     ▼                  ▼                   ▼
Phase 4 ──────────────▶ Phase 5 ─────────▶ Phase 6
(Refactor)            (Docs)              (Verify)
```

All Phase 1-3 tasks are independent and can be parallelized.

## Estimated Effort

| Phase | Tasks | Est. Hours |
|-------|-------|------------|
| 1. User Management | 5 | 4-6 |
| 2. Nuki Management | 4 | 3-4 |
| 3. Zigbee Management | 3 | 2-3 |
| 4. Refactoring | 3 | 2 |
| 5. Documentation | 1 | 1 |
| 6. Verification | 2 | 2-3 |
| **Total** | **18** | **14-19** |