## Context

The Smart Door Finger (SDF) firmware currently supports up to 4095 fingerprint users (`SDF_FINGERPRINT_USER_ID_MAX = 0x0FFF`). This is the sensor's hardware maximum, but the practical use case is a residential smart lock with typically < 10 users.

Current static RAM usage in `sdf_services.c`:
- 4 × 512-element static arrays = 3072 bytes (documented optimization #16 TODO)
- Dynamic allocations in `sdf_app_update_zigbee_user_list()` using `calloc(4096, ...)`
- Stack arrays in CLI `cmd_user_add()` of size 4096 each

The request is to:
1. Reduce max users from 4095 to 10
2. Implement optimization #16: bitmap + packed permissions (~50% RAM savings)
3. Eliminate all dynamic allocations in hot paths

## Goals / Non-Goals

**Goals:**
- Reduce `SDF_FINGERPRINT_USER_ID_MAX` to 10
- Replace 3072-byte static buffers with compact representation (~30 bytes + bitmap)
- Eliminate `calloc()` in `sdf_app_update_zigbee_user_list()`
- Reduce stack usage in CLI commands
- Update all validation bounds and error messages
- Maintain backward compatibility with sensor protocol (sensor still accepts 1-4095, we just limit to 1-10)

**Non-Goals:**
- Change sensor firmware or protocol
- Modify NVS storage format for user data
- Change Zigbee attribute format (0x4000 still emits `[1:3, 5:1]` format)
- Support migration of existing devices with >10 users (factory reset required)

## Decisions

### 1. Bitmap + Packed Permissions Representation

**Decision:** Use a 16-bit bitmap for occupied user IDs (bits 0-9 for IDs 1-10) + packed 2-bit permissions in a `uint8_t[4]` array (8 users per byte, 2 bits each).

**Rationale:**
- 10 users fit in 16 bits (2 bytes) for bitmap
- 10 users × 2 bits = 20 bits → 3 bytes for packed permissions (use 4 bytes for alignment)
- Total: ~6 bytes vs 3072 bytes = 99.8% reduction
- O(1) lookup for ID availability, admin check, permission get/set
- Simple bit manipulation, no loops needed for common operations

**Alternatives Considered:**
- `uint16_t user_ids[10] + uint8_t permissions[10]` (30 bytes): Simpler but O(n) lookup
- Full 4096-bit bitmap (512 bytes): Overkill for 10 users, still 170× larger than needed
- Keep current 512-entry arrays: Defeats the purpose of optimization #16

### 2. Helper Macros and Inline Functions

**Decision:** Add the following in `sdf_services.c`:

```c
#define SDF_SERVICES_MAX_USERS 10
#define SDF_SERVICES_BMP_BITS 16  // uint16_t

#define SDF_SERVICES_BMP_TEST(bmp, id)  ((bmp) & (1u << ((id) - 1)))
#define SDF_SERVICES_BMP_SET(bmp, id)   ((bmp) |= (1u << ((id) - 1)))
#define SDF_SERVICES_BMP_CLEAR(bmp, id) ((bmp) &= ~(1u << ((id) - 1)))

// Packed permissions: 2 bits per user (0=unused, 1=std, 2=elev, 3=admin)
static inline uint8_t sdf_services_perm_get(const uint8_t *packed, uint16_t id) {
    uint8_t byte = packed[(id - 1) / 4];
    uint8_t shift = ((id - 1) % 4) * 2;
    return (byte >> shift) & 0x3;
}

static inline void sdf_services_perm_set(uint8_t *packed, uint16_t id, uint8_t perm) {
    uint8_t *byte = &packed[(id - 1) / 4];
    uint8_t shift = ((id - 1) % 4) * 2;
    *byte = (*byte & ~(0x3 << shift)) | ((perm & 0x3) << shift);
}
```

### 3. Buffer Declarations

```c
// Enrollment query buffers
static uint16_t s_enrollment_user_bmp = 0;
static uint8_t s_enrollment_perm_packed[4] = {0};

// Permission change query buffers
static uint16_t s_perm_user_bmp = 0;
static uint8_t s_perm_perm_packed[4] = {0};
```

### 4. Finding Free User ID

```c
static uint16_t sdf_services_find_free_id(uint16_t occupied_bmp) {
    for (uint16_t id = 1; id <= SDF_SERVICES_MAX_USERS; id++) {
        if (!(occupied_bmp & (1u << (id - 1)))) {
            return id;
        }
    }
    return 0; // none available
}
```

### 5. Zigbee Sync Stack Allocation

In `sdf_app.c`, `sdf_app_update_zigbee_user_list()`:
- Replace `calloc(max_users, ...)` with stack arrays `uint16_t user_ids[11]`, `uint8_t perms[11]`
- Max users = 11 (IDs 1-10, plus index 0 unused)
- 255-byte output buffer sufficient for 10 users (max ~40 chars)

### 6. CLI Stack Arrays

In `sdf_cli_commands.c`, `cmd_user_add()`:
- Replace `uint16_t user_ids[4096]` with `uint16_t user_ids[11]`
- Replace `uint8_t permissions[4096]` with `uint8_t permissions[11]`

## Risks / Trade-offs

| Risk | Mitigation |
|------|------------|
| Existing devices with >10 users cannot upgrade without factory reset | Document as breaking change; factory reset clears all users |
| Sensor protocol still returns IDs 1-4095; we must filter | Add bounds check in `fp_query_users()` callback or in `sdf_services_query_users()` |
| Bitmap operations assume little-endian bit order | ESP32-C6 is little-endian; document assumption |
| Permission packing uses 2 bits (0-3), matches current 1-3 range | 0 = empty, 1 = standard, 2 = elevated, 3 = admin |
| Zigbee attribute 0x4000 format unchanged | Only the internal buffer changes; output format same |
| Test updates required | Update test expectations in parallel with implementation |

## Migration Plan

1. **Code Changes** (single commit):
   - Update `fingerprint.h` constant
   - Refactor `sdf_services.c` buffers and helpers
   - Update `sdf_app.c` stack allocation
   - Update `sdf_cli_commands.c` arrays and validation
   - Update test expectations

2. **Build & Test**:
   - `idf.py build` → verify compilation
   - `idf.py -p <port> flash monitor` on test device
   - Run test_runner suite

3. **Deploy**:
   - Factory reset required for existing devices (>10 users)
   - New devices ship with 10-user limit

## Open Questions

1. **Should `SDF_FINGERPRINT_USER_ID_MIN` stay at 1?** Yes, sensor protocol uses 1-based IDs.

2. **What happens if sensor returns ID > 10?** The sensor firmware won't return IDs we never enrolled. Our enrollment only uses 1-10. But add defensive check in `sdf_services_query_users()`.

3. **NVS storage for user list?** Currently users are stored on the sensor itself, not in ESP32 NVS. The ESP32 only stores permission mapping. No NVS migration needed.

4. **Zigbee `Set PIN Code` with User ID > 10?** The Zigbee handler should reject with error before calling enrollment. Add validation in `sdf_protocol_zigbee.c`.

5. **Documentation version?** Update `doc/user_manual.md` and `doc/sdf_sas.md` in same PR.
