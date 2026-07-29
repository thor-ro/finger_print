## Why

The Smart Door Finger (SDF) firmware currently supports up to **4095 users** (`SDF_FINGERPRINT_USER_ID_MAX = 0x0FFF`) with static RAM buffers sized for **512 users** (3072 bytes total across 4 buffers in `sdf_services.c`). This is excessive for a residential smart lock where typical user counts are < 20.

Optimization #16 in the project's known optimizations list documents: *"3072 bytes static RAM buffers — ⚠️ Documented only — Added comment with TODO for compaction (~50% savings possible with bitmap + packed perms)"*. This change implements that optimization while reducing the max user limit from 4095 to 10, aligning the buffer sizes with realistic deployment needs.

**Benefits:**
- ~50% RAM savings on static buffers (3072 → ~1536 bytes)
- Simplified bounds checking and validation (max 10 users)
- Reduced attack surface for buffer overreads
- Aligns with Zigbee user list attribute (0x4000) which currently reports `[1:3, 5:1]` format

## What Changes

### BREAKING: Reduce `SDF_FINGERPRINT_USER_ID_MAX` from 4095 to 10
- Fingerprint sensor hardware supports IDs up to 4095, but we restrict firmware to 10
- Affects all user ID validation, CLI commands, Zigbee sync, enrollment logic

### Optimize static buffers in `sdf_services.c` (~50% RAM reduction)
- Replace 4 × 512-entry arrays with bitmap (16 bytes for 10 bits) + packed 2-bit permissions (3 bytes)
- Implement helper functions for bitmap/permission manipulation

### Update dependent code paths
- `sdf_app.c`: `sdf_app_update_zigbee_user_list()` - reduce allocation size
- `sdf_cli/sdf_cli_commands.c`: Stack-allocated arrays in `cmd_user_add`, bounds validation
- Test files: Update user ID bounds in validation tests

### Capabilities

#### New Capabilities
- None (this is an optimization, not a new feature)

#### Modified Capabilities
- `sdf-services-tasks`: Buffer size requirements change; validation bounds change
- `security-event-emission`: No requirement changes (events unchanged)
- `security-event-unification`: No requirement changes
- `task-architecture`: Stack sizes may be slightly reducible (smaller buffers = less stack pressure)

## Impact

### Affected Files
| File | Changes |
|------|---------|
| `firmware/components/sdf_drivers/include/fingerprint.h` | Change `SDF_FINGERPRINT_USER_ID_MAX` from `0x0FFF` to `10` |
| `firmware/components/sdf_services/src/sdf_services.c` | Replace 4×512 static buffers with bitmap + packed perms; add helper macros/functions |
| `firmware/components/sdf_app/src/sdf_app.c` | Reduce `calloc` sizes in `sdf_app_update_zigbee_user_list` |
| `firmware/components/sdf_cli/sdf_cli_commands.c` | Reduce stack arrays in `cmd_user_add`; update validation bounds |
| `firmware/components/sdf_drivers/test/test_driver_protocol.c` | Update test bounds for `user_id_valid` tests |
| `firmware/components/sdf_cli/test/test_sdf_cli.c` | Update test bounds for CLI user commands |

### API/ABI Changes
- **Breaking**: `SDF_FINGERPRINT_USER_ID_MAX` value changes from 4095 → 10
- **Breaking**: Maximum enrollable users reduced from 4095 → 10
- Non-breaking: Internal buffer representation changes (opaque to callers)