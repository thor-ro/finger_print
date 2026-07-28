## Priority Alignment Tasks

### 1. Fix sdf_match Task Priority
- [x] Change `SDF_MATCH_TASK_PRIORITY` from 6 to 5 in `firmware/components/sdf_services/src/sdf_services_match.c:23`
- [x] Verify build succeeds

### 2. Fix sdf_enroll Task Priority
- [x] Change `SDF_ENROLL_TASK_PRIORITY` from 5 to 4 in `firmware/components/sdf_services/src/sdf_services_enroll.c:21`
- [x] Verify build succeeds

### 3. Fix sdf_admin Task Priority
- [x] Change `SDF_ADMIN_TASK_PRIORITY` from 6 to 5 in `firmware/components/sdf_services/src/sdf_services_admin.c`
- [x] Verify build succeeds

### 4. Fix sdf_button Task Priority
- [x] Change `SDF_BUTTON_TASK_PRIORITY` from 5 to 4 in `firmware/components/sdf_services/src/sdf_services_button.c`
- [x] Verify build succeeds

### 5. Verify doc/rtos_tasks.md Consistency
- [x] Confirm all documented priority values match corrected code values
- [x] Update doc/rtos_tasks.md if any values still differ

### 6. Hardware Validation
- [x] Build with debug config and flash to hardware
- [ ] Measure lock action latency after fingerprint match (target: < 2s)
- [ ] Verify no priority inversion with Zigbee commands
- [ ] Verify admin auth still preempts match cycle
- [ ] Monitor task watchdog for any stalls after changes

### 7. Documentation Sync
- [x] Update `doc/rtos_tasks.md` if needed
- [x] Update `doc/sdf_sas.md` §6 if needed
- [x] Update AGENTS.md if priority constants change