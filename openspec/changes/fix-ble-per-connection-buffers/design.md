## Context

With 3 simultaneous connections possible, a single global buffer means:
- Connection A writes to Config characteristic: global buffer updated
- Connection B reads Config: gets A's write, not its own response
- Both connections see the same auth value, which is a security concern

Moving buffers into the connection struct is the clean fix.

## Goals / Non-Goals

**Goals:**
- Isolate per-connection characteristic values so reads and writes for one connection don't affect others
- No change in wire protocol or feature set

**Non-Goals:**
- Reducing the 512-byte `SDF_BLE_COMPANION_ATTR_MAX_LEN` (tracked separately)

## Decisions

**Add per-connection value buffers to `sdf_ble_companion_connection_t`:**

```c
typedef struct {
    uint16_t conn_handle;
    bool connected;
    sdf_ble_companion_auth_state_t auth_state;
    bool auth_pending;
    char username[SDF_STORAGE_WEB_USER_NAME_MAX];
    uint8_t password_hash[SDF_STORAGE_WEB_USER_HASH_LEN];
    // New:
    uint8_t auth_value[SDF_BLE_COMPANION_ATTR_MAX_LEN];
    uint16_t auth_value_len;
    uint8_t config_value[SDF_BLE_COMPANION_ATTR_MAX_LEN];
    uint16_t config_value_len;
    uint8_t enroll_value[SDF_BLE_COMPANION_ATTR_MAX_LEN];
    uint16_t enroll_value_len;
    uint8_t ota_value[SDF_BLE_COMPANION_ATTR_MAX_LEN];
    uint16_t ota_value_len;
} sdf_ble_companion_connection_t;
```

RAM impact: 3 connections × (4 × 512 bytes + 4 × 2 bytes) = ~6152 bytes total. Previously 4 × 512 = 2048 bytes global. Net increase: ~4104 bytes. Evaluate if this is acceptable given the ESP32-C6's 512KB SRAM.

Alternative: Use a single global response buffer per characteristic (not per-connection), protected by a mutex, with a per-connection sequence number — more complex, less clean. Rejected.

## Risks / Trade-offs

- [RAM increase] ~4KB additional RAM. On ESP32-C6 with 512KB SRAM this is acceptable but should be monitored. The existing global buffers can be removed to reclaim their 2KB.
