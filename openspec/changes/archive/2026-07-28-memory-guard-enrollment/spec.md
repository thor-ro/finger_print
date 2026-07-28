## Memory Guard for Enrollment Specification

### Current State

`sdf_services.c` uses `calloc()` in hot paths without OOM handling:

1. `sdf_services_start_local_enrollment_with_permission()`: Allocates 4096 * (2 + 1) = 12288 bytes for user query buffers
2. `sdf_services_change_user_permission()`: Allocates 4096 * (2 + 1) = 12288 bytes for user lookup buffers

If either allocation fails, the function returns early with an error, but:
- The match cycle is unaffected (no allocation there)
- The enrollment path fails silently (red LED only)
- No audit event for OOM condition

### Target State

1. **Static buffers for query results**: Replace `calloc()` with static buffers sized for max user capacity
2. **Heap guard**: Check `esp_get_free_heap_size()` before any allocation in hot paths
3. **OOM audit events**: Emit `SDF_AUDIT_PROTOCOL_ERROR` when heap is insufficient
4. **Graceful degradation**: When OOM is detected, refuse the operation rather than crashing

### Static Buffer Design

The fingerprint sensor supports max 500 users (SDF_FINGERPRINT_USER_ID_MAX = 0x0FFF, but practical limit is 500). A static buffer for 500 users:
- `user_ids`: 500 * sizeof(uint16_t) = 1000 bytes
- `permissions`: 500 * sizeof(uint8_t) = 500 bytes
- Total: 1500 bytes per buffer

This is a one-time cost in BSS/data segment, no runtime allocation.

### Verification Points

1. No `calloc()` in match cycle (already correct)
2. No `calloc()` in enrollment query path
3. No `calloc()` in permission change path
4. OOM guard triggers correctly when heap < threshold
5. Audit events emitted on OOM
6. Device remains operational after OOM condition