## 1. Create Canonical Task Reference Document

- [x] 1.1 Create `doc/rtos_tasks.md` with full task architecture table
- [x] 1.2 Add detailed specifications for sdf_power_task
- [x] 1.3 Add detailed specifications for sdf_zigbee_task
- [x] 1.4 Add detailed specifications for sdf_match_task
- [x] 1.5 Add detailed specifications for sdf_enroll_task
- [x] 1.6 Add detailed specifications for sdf_admin_task
- [x] 1.7 Add detailed specifications for sdf_button_task
- [x] 1.8 Add detailed specifications for sdf_ota_task (future)
- [x] 1.9 Add event router contracts (priority mapping, queue depths)
- [x] 1.10 Add migration path documentation (3 phases)
- [x] 1.11 Add stack monitoring and watchdog assignments
- [x] 1.12 Add priority inversion analysis

## 2. Update Existing Documentation

- [x] 2.1 Update `doc/sdf_sas.md` §6 Runtime View with canonical task table
- [x] 2.2 Update `doc/software-architecture.md` §6 Runtime Design to match actual implementation
- [x] 2.3 Update `AGENTS.md` Component Structure to list all 6 tasks with owners

## 3. Validate Stack Sizes on Hardware

- [ ] 3.1 Build firmware with debug config (`idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.debug.defaults" build`)
- [ ] 3.2 Flash and run on ESP32-C6 hardware
- [ ] 3.3 Measure `uxTaskGetStackHighWaterMark()` for sdf_power_task
- [ ] 3.4 Measure `uxTaskGetStackHighWaterMark()` for sdf_zigbee_task
- [ ] 3.5 Measure `uxTaskGetStackHighWaterMark()` for sdf_services_task (current monolithic)
- [ ] 3.6 Record high-water marks and validate against design targets
- [ ] 3.7 Update `doc/rtos_tasks.md` with validated stack margins

## 4. Verification

- [x] 4.1 Verify all task specs match proposal.md
- [x] 4.2 Verify event router contracts are internally consistent
- [x] 4.3 Verify migration path aligns with add-event-router and refactor-services-task changes
- [x] 4.4 Verify documentation cross-references are correct
- [x] 4.5 Run `idf.py build` to confirm no build regressions