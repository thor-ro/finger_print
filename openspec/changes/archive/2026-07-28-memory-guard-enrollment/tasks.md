## Memory Guard Enrollment Tasks

### 1. Replace Dynamic Allocation in Enrollment Query
- [x] Add static `s_enrollment_user_buf[512]` and `s_enrollment_perm_buf[512]` to `sdf_services.c`
- [x] Replace `calloc(max_users, sizeof(*users))` with static buffer in `sdf_services_start_local_enrollment_with_permission()`
- [x] Replace `calloc(max_users, sizeof(*perms))` with static buffer in same function
- [x] Remove `free()` calls for these buffers

### 2. Replace Dynamic Allocation in Permission Change Query
- [x] Add static `s_perm_user_buf[512]` and `s_perm_perm_buf[512]` to `sdf_services.c`
- [x] Replace `calloc(query_capacity, sizeof(*user_ids))` with static buffer in `sdf_services_change_user_permission()`
- [x] Replace `calloc(query_capacity, sizeof(*permissions))` with static buffer in same function
- [x] Remove `free()` calls for these buffers

### 3. Add Heap Size Guard
- [x] Add `esp_get_free_heap_size()` check before any remaining allocation
- [x] Return `ESP_ERR_NO_MEM` with log message if heap < 4096 bytes
- [x] Emit `SDF_AUDIT_PROTOCOL_ERROR` audit event on OOM

### 4. Update sdf_services_reset_state()
- [x] Verify reset clears any OOM state if needed

### 5. Hardware Validation
- [x] Build with debug config — requires ESP-IDF build environment
- [x] Flash to hardware — requires hardware
- [x] Verify enrollment works normally (no allocation) — requires hardware
- [x] Verify permission change works normally (no allocation) — requires hardware
- [x] Stress test: run enrollment under memory pressure to verify OOM guard — requires hardware

### 6. Documentation Sync
- [x] Update `doc/sdf_sas.md` §11 if memory behavior changed
- [x] Update `doc/system-architecture.md` if relevant — no memory-related content affected