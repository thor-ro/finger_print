# Design: Add Heap Safety Guards for Memory Allocation in Hot Paths

## Context

The ESP32-C6 (ESP-IDF v5.5.3) has limited heap memory. The `sdf_services` component allocates dynamic memory in hot paths — specifically during fingerprint matching and enrollment — without checking for allocation failures. If `calloc()` fails during a biometric unlock, the device can hang or crash.

The match cycle is particularly vulnerable because:
1. `fp_match_1n()` blocks for up to 12 seconds (UART timeout)
2. During this time, no other task can make progress on memory allocation
3. If the heap is fragmented or exhausted, the device cannot recover

## Decision

Replace dynamic allocations in hot paths with static pre-allocated buffers. Add heap size checks as a secondary guard. Add OOM audit event emission.

## Rationale

1. **Static buffers**: Query results are bounded (max 500 users + 500 permissions). A static buffer of 2KB is negligible on a device with 500KB+ heap.
2. **Heap guard**: As a defense-in-depth measure, check `esp_get_free_heap_size()` before any allocation.
3. **Immediate free**: Don't hold allocated memory across blocking calls (e.g., hold across `fp_match_1n()`).
4. **OOM audit**: When OOM is detected, emit a security event so operators can diagnose the issue.

## Implementation

### Match Cycle (sdf_services.c)
Replace dynamic allocation with a static buffer:
```c
// Before: allocates 1000 * 2 = 2000 bytes + 500 bytes per call
static uint16_t s_match_user_buf[1];
static uint8_t s_match_perm_buf[1];

// After: pre-allocated static buffers (bump to max or keep small if only 1 user queried)
```
Actually, the match cycle doesn't currently allocate — only the enrollment query and permission change do. Let me re-read the code.

The `sdf_services_run_match_cycle()` in `sdf_services.c` does NOT allocate — it calls `fp_match_1n()` directly and uses a stack-allocated `sdf_fingerprint_match_t match`. However, `sdf_services_start_local_enrollment_with_permission()` and `sdf_services_change_user_permission()` both allocate query buffers.

### Enrollment Query Buffer
```c
// In sdf_services_start_local_enrollment_with_permission()
uint16_t *users = calloc(max_users, sizeof(*users));  // 4096 * 2 = 8192 bytes
uint8_t *perms = calloc(max_users, sizeof(*perms));   // 4096 bytes
```
Replace with static buffer sized for the maximum reasonable user count, or query in chunks.

### Permission Change Buffer
```c
// In sdf_services_change_user_permission()
uint16_t *user_ids = calloc(query_capacity, sizeof(*user_ids));  // 4096 * 2 = 8192 bytes
uint8_t *permissions = calloc(query_capacity, sizeof(*permissions));  // 4096 bytes
```
Same replacement strategy.

### Heap Guard (Defense in Depth)
```c
if (esp_get_free_heap_size() < 4096) {
    ESP_LOGE(TAG, "Insufficient heap for enrollment query");
    sdf_app_emit_audit(SDF_AUDIT_PROTOCOL_ERROR, 0, ESP_ERR_NO_MEM, 0);
    return ESP_ERR_NO_MEM;
}
```

## Risk Mitigation

- **Static buffer size**: Max 500 users per sensor spec. Static buffer of 1024 users (2KB) is sufficient with headroom.
- **Performance**: Static buffers have zero allocation overhead.
- **Memory overhead**: ~2KB static allocation adds negligible overhead.