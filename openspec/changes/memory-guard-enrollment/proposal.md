# Proposal: Add Heap Safety Guards for Memory Allocation in Hot Paths

## Summary

Add compile-time and runtime guards against memory allocation failures in `sdf_services.c`, which currently calls `calloc()` in hot paths (match cycle, enrollment, permission change) without checking for allocation failures. On a memory-constrained ESP32-C6, a failed `calloc()` during a biometric unlock could crash the device.

## Problem

`sdf_services.c` uses `calloc()` in several critical paths without any failure handling:

1. **Match cycle** (`sdf_services_run_match_cycle`): Uses `fp_match_1n()` which blocks up to 12s; if `calloc()` fails here, the device may hang or panic
2. **Enrollment** (`sdf_services_start_local_enrollment_with_permission`): Allocates user query buffers without fallback
3. **Permission change** (`sdf_services_change_user_permission`): Allocates query buffers for user lookup without fallback

The ESP-IDF environment has a limited heap (~500KB after Wi-Fi/BLE/Zigbee stack), and fragmentation can cause `calloc()` to fail during prolonged operation.

## Solution

1. **Free memory before blocking calls**: In the match cycle, allocate query buffers before calling `fp_match_1n()` and free them immediately after
2. **Add OOM guards**: Check `esp_get_free_heap_size()` before large allocations
3. **Use static buffers where possible**: Replace dynamic allocations with statically sized buffers for query results (max 500 users = 1000 uint16_t + 500 uint8_t)
4. **Log OOM events**: Emit audit events for out-of-memory conditions

## Architecture Impact

### sdf_services.c Changes
- Replace dynamic `calloc()` in match cycle with pre-allocated static buffers
- Add heap size check before any `calloc()` in hot paths
- Free allocated memory immediately after use (don't hold across blocking calls)
- Add OOM audit event handling

### sdf_services.h Changes (if needed)
- No API signature changes, but internal implementation details change

## Benefits

1. **Reliability**: Device doesn't crash on memory exhaustion during fingerprint matching
2. **Predictability**: Static allocations guarantee memory availability for critical paths
3. **Observability**: OOM events are logged to audit trail
4. **Performance**: Pre-allocated buffers eliminate heap fragmentation risk
5. **Security**: No denial-of-service via memory exhaustion during active lockout periods

## Acceptance Criteria

- [ ] No `calloc()` failures in match cycle path
- [ ] No `calloc()` failures in enrollment path
- [ ] No `calloc()` failures in permission change path
- [ ] OOM events logged to audit trail
- [ ] Static buffer sizes sufficient for max user count (500)
- [ ] All existing tests pass
- [ ] Memory usage does not increase beyond static allocation overhead