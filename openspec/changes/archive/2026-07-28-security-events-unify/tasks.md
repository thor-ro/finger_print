## Security Events Unification Tasks

### 1. Remove Legacy Callback from sdf_services Config
- [x] Remove `sdf_services_security_event_cb` from `sdf_services_config_t` in `sdf_services.h`
- [x] Remove `security_event_cb` and `security_event_ctx` fields from config struct
- [x] Remove `sdf_services_get_default_config()` initialization of security_event_cb

### 2. Remove sdf_services_notify_security_event()
- [x] Delete `sdf_services_notify_security_event()` from `sdf_services.c`
- [x] Update `sdf_services_run_match_cycle()` to emit security events directly via event router
- [x] Update match success/failure/lockout emission paths in match cycle

### 3. Remove sdf_match_task_notify_security_event()
- [x] Delete `sdf_match_task_notify_security_event()` from `sdf_services_match.c`
- [x] Update match cycle in `sdf_match_task_run_match_cycle()` to emit events directly via event router
- [x] Ensure lockout cleared events still emitted correctly

### 4. Update sdf_app Security Event Handling
- [x] Replace `sdf_on_security_event` callback with event router subscription
- [x] Subscribe to `SDF_EVENT_ROUTER_BIOMETRIC_MATCH_FAILED` at HIGH priority
- [x] Subscribe to `SDF_EVENT_ROUTER_SECURITY_LOCKOUT` at CRITICAL priority
- [x] Route alarm mask and audit logic through subscription callbacks

### 5. Update sdf_services_start_tasks()
- [x] Remove any task creation dependencies on legacy callback paths

### 6. Update Documentation
- [x] Update `doc/sdf_sas.md` §7 to reflect single-path security events
- [x] Update `doc/rtos_tasks.md` if security event flow changes
- [x] Update `doc/system-architecture.md` event flow diagrams

### 7. Hardware Validation
- [ ] Build with debug config and flash to hardware
- [ ] Verify lockout triggers correctly (5 failures in 60s)
- [ ] Verify alarm bits set correctly on Zigbee
- [ ] Verify no duplicate audit entries
- [ ] Verify match success events still trigger unlock flow