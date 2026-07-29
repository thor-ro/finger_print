## 1. Update Constants

- [x] 1.1 Change `SDF_FINGERPRINT_USER_ID_MAX` from `0x0FFFu` to `10u` in `firmware/components/sdf_drivers/include/fingerprint.h`
- [x] 1.2 Add `SDF_SERVICES_MAX_USERS` and `SDF_SERVICES_MAX_USERS_ALIGNED` constants in `firmware/components/sdf_services/src/sdf_services.c`

## 2. Refactor Static Buffers in sdf_services.c

- [x] 2.1 Replace 4×512 static arrays with bitmap + packed permissions:
  - `s_enrollment_user_bmp` (uint16_t)
  - `s_enrollment_perm_packed` (uint8_t[4])
  - `s_perm_user_bmp` (uint16_t)
  - `s_perm_perm_packed` (uint8_t[4])
- [x] 2.2 Add helper macros: `SDF_SERVICES_BMP_SET`, `SDF_SERVICES_BMP_CLEAR`, `SDF_SERVICES_BMP_TEST`
- [x] 2.3 Add inline functions: `sdf_services_perm_get()`, `sdf_services_perm_set()`, `sdf_services_pack_user_list()`
- [x] 2.4 Update `sdf_services_start_local_enrollment_with_permission()` to use new buffers and find free ID via bitmap
- [x] 2.5 Update `sdf_services_change_user_permission()` to use new buffers for admin count and user lookup
- [x] 2.6 Update `sdf_services_query_users()` call sites to pass `SDF_SERVICES_MAX_USERS` as max_count

## 3. Update sdf_app.c Zigbee User List Sync

- [x] 3.1 Replace dynamic `calloc()` in `sdf_app_update_zigbee_user_list()` with stack arrays sized to max 11 users
- [x] 3.2 Update `max_users` calculation to use `SDF_FINGERPRINT_USER_ID_MAX + 1` (now 11)
- [x] 3.3 Verify 255-byte output buffer is sufficient for 10 users

## 4. Update CLI Commands

- [x] 4.1 Reduce stack arrays in `cmd_user_add()` from `[4096]` to `[11]`
- [x] 4.2 Update all `parse_uint16_arg()` calls to use new `SDF_FINGERPRINT_USER_ID_MAX` (10) as upper bound
- [x] 4.3 Update error messages to show "Expected 1-10" instead of "Expected 1-4095"

## 5. Update Tests

- [x] 5.1 Update `firmware/components/sdf_drivers/test/test_driver_protocol.c`:
  - `test_user_id_valid_max` expects 10
  - `test_user_id_invalid_above_max` tests 11
  - `test_user_id_invalid_uint16_max` tests 65535
- [x] 5.2 Update `firmware/components/sdf_cli/test/test_sdf_cli.c`:
  - All user ID validation tests use new bounds (1-10)
- [ ] 5.3 Add test for bitmap/permission packing helpers (optional)

## 6. Documentation Updates

- [x] 6.1 Update `doc/user_manual.md` "User Capacity" section: change "1 to 500" → "1 to 10"
- [x] 6.2 Update `doc/sdf_sas.md` if it references user capacity or buffer sizes
- [x] 6.3 Update `AGENTS.md` if it references SDF_MAX_USERS or buffer sizes

## 7. Build Verification

- [x] 7.1 Run `idf.py build` - verify no compilation errors
- [ ] 7.2 Run test_runner build - verify all tests compile
- [ ] 7.3 Flash to device and run manual tests:
  - [ ] Enroll 10 users sequentially
  - [ ] Attempt 11th enrollment → should fail with red LED
  - [ ] Delete user 3, enroll new → should get ID 3
  - [ ] CLI `user add 11 1` → rejects with "Expected 1-10"
  - [ ] Zigbee user list sync works with 10 users
  - [ ] Permission change works correctly

## 8. Verification

- [ ] 8.1 Verify RAM savings: check map file for static data reduction
- [ ] 8.2 Verify no heap allocation in hot paths (match cycle, enrollment query, permission change)
- [ ] 8.3 Verify all existing tests pass
- [ ] 8.4 Run full integration test: fingerprint → unlock → BLE → Nuki