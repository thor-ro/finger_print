## Context

The SDF v2.0 firmware has a `FACTORY_RESET` admin action defined but not implemented (TODO in `sdf_app.c:488`). The user wants to complete this functionality using Option B (full NVS erase) for simplicity and guaranteed clean slate.

**Current State:**
- `sdf_services` handles button press (8s hold → `pending_admin_action = FACTORY_RESET`) and admin authorization
- `sdf_app.on_admin_action()` has the TODO at line 488
- `sdf_storage` has `sdf_storage_nuki_clear()` (selective erase)
- `sdf_drivers` has `fp_delete_all_users()` (works)
- `sdf_protocol_zigbee` has no factory reset function
- `sdf_services` has no state reset function
- `sdf_cli` has no factory reset command

**Constraints:**
- ESP-IDF v5.5.3
- NVS encryption enabled (nvs_keys partition)
- Single-core ESP32-C6
- Deep sleep default; must wake to execute reset
- Tests require hardware (test_runner project)

## Goals / Non-Goals

**Goals:**
- Complete the factory reset implementation
- Use Option B (full NVS erase via `nvs_flash_erase()`)
- Reboot to UNCLAIMED state (0 users, LED breathes WHITE)
- Add CLI command for manual reset
- All specs pass review

**Non-Goals:**
- Selective erase (Option A) — Option B chosen
- OTA rollback support
- Factory reset via Zigbee command (not in ZHA Door Lock cluster)
- Preserve any data across reset

## Decisions

### 1. Option B: Full NVS Erase

**Rationale:** Simpler implementation, guaranteed clean state, no key maintenance burden. The NVS partition is dedicated to SDF (no other data stored).

**Alternative Considered:** Option A (selective erase) — rejected due to maintenance overhead; Option C (namespace erase) — requires ESP-IDF 5.x `nvs_erase_all()`, less tested.

### 2. Zigbee Factory Reset via `esp_zb_bdb_factory_reset()`

**Rationale:** This is the ESP-Zigbee SDK provided function for factory reset. It erases NVRAM and resets the BDB state.

**Alternative Considered:** Manual `esp_zb_bdb_leave_network()` + erase partition — rejected, SDK function is canonical.

### 3. Reboot via `esp_restart()`

**Rationale:** Cleanest way to reinitialize all subsystems (NVS, Zigbee, BLE, drivers). Watchdog would also work but `esp_restart()` is explicit.

**Alternative Considered:** Let watchdog timeout — rejected, unpredictable timing.

### 4. CLI Confirmation with "YES"

**Rationale:** Prevents accidental resets. No TOTP or second factor needed (physical access required).

**Alternative Considered:** No confirmation — rejected, too dangerous; Button sequence confirmation — rejected, CLI is separate interface.

### 5. Reset Order: NVS → Fingerprint → Zigbee → State → Reboot

**Rationale:** Erase persistent storage first, then hardware, then in-memory state, then reboot. If any step fails, reboot still clears RAM.

## Risks / Trade-offs

| Risk | Mitigation |
|------|------------|
| NVS erase fails (corrupt partition) | `nvs_flash_erase()` handles corruption; reboot recovers |
| Zigbee factory reset fails | Log error but continue; reboot clears RAM state |
| Fingerprint sensor unresponsive | Log error; sensor is secondary to NVS/Zigbee |
| Power loss during reset | NVS erase is atomic-ish; worst case: partial erase → next boot triggers `nvs_flash_erase()` again |
| CLI command without serial access | Physical button (8s hold) still works |

## Migration Plan

Not applicable — this completes an incomplete feature, no migration needed.

## Open Questions

1. **Zigbee API**: Confirm `esp_zb_bdb_factory_reset()` exists in ESP-IDF v5.5.3 (need to check SDK)
2. **NVS re-init**: Does `nvs_flash_init()` after `nvs_flash_erase()` require security re-validation?
3. **Watchdog**: Should we disable TWDT before long-running erase operations?