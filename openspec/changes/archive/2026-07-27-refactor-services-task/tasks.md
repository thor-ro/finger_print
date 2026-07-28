# Tasks: Refactor sdf_services Monolithic Task

## Progress: 0/14 tasks complete

---

### Phase 1: Event Type Definitions

- [ ] **Task 1.1**: Add new event types to `sdf_event_router_type_t` in `firmware/components/sdf_event_router/include/sdf_event_router.h`
  - Add: BIOMETRIC_MATCH_REQUEST, ENROLLMENT_START, ENROLLMENT_STEP_RESULT, ENROLLMENT_COMPLETE, ENROLLMENT_FAILED, ADMIN_AUTH_RESULT, ADMIN_ACTION_COMPLETE, BUTTON_PRESS, BUTTON_LONG_PRESS, BUTTON_MULTI_PRESS
  - Add corresponding payload structs
  - Extend event union

- [ ] **Task 1.2**: Add new event types to `sdf_event_type_t` in `firmware/components/sdf_common/include/sdf_common.h` (if needed for backward compat)
  - Note: New events go through event router, but may need sdf_common for audit/security events

---

### Phase 2: Task Infrastructure

- [ ] **Task 2.1**: Add task declarations to `firmware/components/sdf_services/include/sdf_services.h`
  - `esp_err_t sdf_services_start_tasks(void);`
  - `esp_err_t sdf_services_stop_tasks(void);`

- [ ] **Task 2.2**: Add internal task function declarations to `firmware/components/sdf_services/include/sdf_services_internal.h`
  - `void sdf_match_task(void *arg);`
  - `void sdf_enroll_task(void *arg);`
  - `void sdf_admin_task(void *arg);`
  - `void sdf_button_task(void *arg);`

- [ ] **Task 2.3**: Add task configuration constants to `firmware/components/sdf_services/src/sdf_services.c`
  - Stack sizes, priorities, names for 4 tasks
  - Task handles in s_state

---

### Phase 3: Task Implementations

- [ ] **Task 3.1**: Implement `sdf_match_task` in `firmware/components/sdf_services/src/sdf_services_match.c` (new file)
  - Subscribe to: BIOMETRIC_MATCH_REQUEST, POWER_WAKE, POWER_SLEEP
  - Emit: BIOMETRIC_MATCH, BIOMETRIC_MATCH_FAILED, SECURITY_LOCKOUT
  - 400ms poll loop with cooldown/lockout logic
  - Migrate logic from `sdf_services_run_match_cycle()`

- [ ] **Task 3.2**: Implement `sdf_enroll_task` in `firmware/components/sdf_services/src/sdf_services_enroll.c` (new file)
  - Subscribe to: ENROLLMENT_START, ENROLLMENT_STEP_RESULT, POWER_WAKE, POWER_SLEEP
  - Emit: ENROLLMENT_STEP_COMPLETE, ENROLLMENT_COMPLETE, ENROLLMENT_FAILED
  - Event-driven enrollment state machine
  - Migrate logic from `sdf_services_run_enrollment_step()` and `sdf_services_start_pending_enrollment_if_any()`

- [ ] **Task 3.3**: Implement `sdf_admin_task` in `firmware/components/sdf_services/src/sdf_services_admin.c` (new file)
  - Subscribe to: ADMIN_ACTION_REQUEST, BIOMETRIC_MATCH, POWER_WAKE, POWER_SLEEP
  - Emit: ADMIN_AUTH_RESULT, ADMIN_ACTION_COMPLETE, SECURITY_LOCKOUT
  - 10s timeout handling
  - Migrate logic from `sdf_services_run_admin_auth_cycle()` and `sdf_services_execute_admin_action()`

- [ ] **Task 3.4**: Implement `sdf_button_task` in `firmware/components/sdf_services/src/sdf_services_button.c` (new file)
  - GPIO ISR + debounce timer
  - Subscribe to: POWER_WAKE, POWER_SLEEP
  - Emit: BUTTON_PRESS, BUTTON_LONG_PRESS, BUTTON_MULTI_PRESS, ADMIN_ACTION_REQUEST
  - Migrate logic from `sdf_services_btn_cb()` and button registration in `sdf_services_init()`

---

### Phase 4: Integration & Wiring

- [ ] **Task 4.1**: Implement `sdf_services_start_tasks()` in `sdf_services.c`
  - Create 4 tasks with correct config
  - Store handles in s_state
  - Return ESP_OK/ESP_FAIL

- [ ] **Task 4.2**: Implement `sdf_services_stop_tasks()` in `sdf_services.c`
  - Delete tasks, clean up handles
  - Return ESP_OK

- [ ] **Task 4.3**: Modify `sdf_services_init()` to call `sdf_services_start_tasks()`
  - Keep old `sdf_services_task` creation for transition (thin adapter)
  - Remove button registration from init (moved to button task)

- [ ] **Task 4.4**: Create thin adapter `sdf_services_task` that delegates to event router
  - Subscribe to relevant events
  - Forward to new tasks during transition
  - Log deprecation warnings

---

### Phase 5: Testing & Validation

- [ ] **Task 5.1**: Update unit tests in `firmware/components/sdf_services/test/`
  - Test each task in isolation with event injection
  - Mock event router for task tests

- [ ] **Task 5.2**: Add integration test in `firmware/test_runner/`
  - Test: fingerprint match → unlock → BLE action
  - Test: enrollment 3 steps → complete
  - Test: admin auth → action execute
  - Test: button press types

- [ ] **Task 5.3**: Run full test suite
  - `cd firmware/test_runner && idf.py build`
  - `idf.py -p <PORT> flash monitor`
  - Verify all existing tests pass

---

### Phase 6: Documentation

- [ ] **Task 6.1**: Update `doc/sdf_sas.md` sections 5, 6, 8, 9
  - Component architecture diagram
  - Task responsibilities table
  - Event flow diagrams
  - API reference for new functions

- [ ] **Task 6.2**: Update `AGENTS.md` with new component structure
  - Add new task files to component list

---

## Dependencies

- Phase 1 must complete before Phase 3
- Phase 2 must complete before Phase 4
- Phase 3 tasks can be done in parallel
- Phase 4 depends on Phase 3
- Phase 5 depends on Phase 4
- Phase 6 can be done anytime after Phase 3