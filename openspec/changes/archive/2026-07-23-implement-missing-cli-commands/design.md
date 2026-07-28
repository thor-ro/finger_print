# Design: Implement Missing CLI Commands

## Architecture

The CLI commands follow the existing pattern in `sdf_cli_commands.c`:
1. Parse arguments using `argtable3`
2. Check authentication via `check_auth()`
3. Call backend APIs (already implemented in other components)
4. Print human-readable output

No new components, tasks, or FreeRTOS resources needed — purely command handler wiring.

## Component Integration

```
┌─────────────────┐     ┌──────────────────┐
│  sdf_cli        │────▶│  sdf_services    │──▶ fingerprint driver
│  (commands)     │     │  (user mgmt)     │
└─────────────────┘     └──────────────────┘
         │
         ├──────────────────┐
         ▼                  ▼
┌─────────────────┐  ┌──────────────────┐
│ sdf_protocol_ble│  │ sdf_protocol_zigbee│
│ (Nuki mgmt)     │  │ (Zigbee mgmt)    │
└─────────────────┘  └──────────────────┘
```

## Command Implementation Details

### User Commands (`user`)

| Subcommand | Args | Backend Call | Auth Required |
|------------|------|--------------|---------------|
| `list` | — | `sdf_services_query_users()` → format table | CLI login |
| `get <id>` | uint16 | `fp_query_user_permission()` | CLI login |
| `add <id> <perm>` | uint16, uint8 | `sdf_services_request_enrollment()` + `fp_enroll_step()` ×3 | CLI login + admin FP |
| `del <id>` | uint16 | `sdf_services_delete_user()` | CLI login + admin FP |
| `permission <id> <perm>` | uint16, uint8 | `sdf_services_change_user_permission()` (exists) | CLI login + admin FP |

**Enrollment flow for `user add`:**
1. Validate `user_id` not occupied (query users)
2. Call `sdf_services_request_enrollment(user_id, permission)` — sets pending enrollment
3. Prompt user: "Place finger on sensor (scan 1 of 3)..."
4. Call `fp_enroll_step(1, user_id, permission)` → wait for ACK
5. Repeat for steps 2 and 3
6. On success: print confirmation

### Nuki Commands (`nuki`)

| Subcommand | Args | Backend Call | Auth Required |
|------------|------|--------------|---------------|
| `status` | — | `sdf_storage_nuki_load()`, `sdf_nuki_ble_is_ready()` | CLI login |
| `connect` | — | `sdf_nuki_ble_set_enabled(true)`, `sdf_nuki_ble_start()` | CLI login |
| `pair` | — | `sdf_nuki_pairing_init()` → `sdf_nuki_pairing_start()` → wait for completion | CLI login + admin FP |
| `unpair` | — | `sdf_storage_nuki_clear()`, `sdf_storage_ble_target_clear()`, `sdf_nuki_ble_stop()` | CLI login + admin FP |

**Pairing flow for `nuki pair`:**
1. Check if already paired (has credentials)
2. Enable BLE transport
3. Init pairing: `sdf_nuki_pairing_init(&s_pairing, &s_client, 1, SDF_APP_ID, SDF_APP_NAME)`
4. Start: `sdf_nuki_pairing_start(&s_pairing)`
5. Wait for `s_pairing.state == SDF_NUKI_PAIRING_COMPLETE` (poll or callback)
6. Get credentials: `sdf_nuki_pairing_get_credentials(&s_pairing, &creds)`
7. Save: `sdf_storage_nuki_save(creds.authorization_id, creds.shared_key)`
8. Print success with auth ID

### Zigbee Commands (`zigbee`)

| Subcommand | Args | Backend Call | Auth Required |
|------------|------|--------------|---------------|
| `status` | — | `sdf_protocol_zigbee_is_ready()`, ESP Zigbee APIs for PAN/channel/addr | CLI login |
| `connect` | — | `sdf_protocol_zigbee_permit_join()` | CLI login + admin FP |
| `unpair` | — | `sdf_protocol_zigbee_factory_reset()` | CLI login + admin FP |

## Argument Parsing

Reuse existing helpers in `sdf_cli_commands.c`:
- `parse_uint16_arg()` for user IDs
- `parse_uint8_arg()` for permissions (1-3)

Add new argtable3 definitions for each subcommand.

## Error Handling

| Error | User Message |
|-------|--------------|
| `ESP_ERR_NOT_FOUND` | "User <id> not found" |
| `ESP_ERR_INVALID_ARG` | "Invalid user ID (1-4095) or permission (1-3)" |
| `ESP_ERR_INVALID_STATE` | "Device busy / enrollment in progress" |
| `ESP_ERR_TIMEOUT` | "Admin fingerprint timeout (10s)" |
| `SDF_FINGERPRINT_OP_FULL` | "Fingerprint database full (max 4095 users)" |
| `SDF_FINGERPRINT_OP_USER_OCCUPIED` | "User ID <id> already enrolled" |

## Output Formatting

Consistent style:
```
User ID  Permission
-----    ----------
1        3 (Admin)
5        1 (Standard)
```

Status commands use key-value:
```
Paired: yes
Authorization ID: 0xA1B2C3D4
BLE Transport: ready
```

## Testing Strategy

### Unit Tests (Linux host via test_runner)
- Mock `sdf_services`, `fingerprint`, `sdf_protocol_ble`, `sdf_protocol_zigbee` APIs
- Test argument parsing, auth checks, output formatting
- Test error paths (invalid args, auth failures, backend errors)

### Integration Tests (Hardware)
- Full CLI command flows via USB-C
- Verify admin fingerprint auth triggers
- Verify enrollment completes 3 steps
- Verify Nuki pairing stores credentials
- Verify Zigbee join/leave works

## Documentation Updates

Add to `doc/user_manual.md`:
```
## USB-C CLI Commands

### User Management
  user list                    List all enrolled users
  user get <id>                Show user details
  user add <id> <perm>         Enroll new user (requires admin FP)
  user del <id>                Delete user (requires admin FP)
  user permission <id> <perm>  Change permission (requires admin FP)

### Nuki Management
  nuki status                  Show pairing/connection status
  nuki connect                 Connect to paired Nuki lock
  nuki pair                    Pair with Nuki lock (requires admin FP)
  nuki unpair                  Unpair from Nuki lock (requires admin FP)

### Zigbee Management
  zigbee status                Show network status
  zigbee connect               Start network steering (requires admin FP)
  zigbee unpair                Leave network & clear NVRAM (requires admin FP)
```