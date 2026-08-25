## 1. Guard Implementation

- [x] 1.1 In `sdf_services_delete_user()`, take a snapshot of the enrolled-user bitmap and packed permissions before any sensor call, using the same accessor pattern as `sdf_services_change_user_permission()` (`sdf_services.c:1055-1057`)
- [x] 1.2 Return `ESP_ERR_NOT_FOUND` when the user id is not set in the snapshot bitmap, before `fp_delete_user()` is reached
- [x] 1.3 Count admins from the snapshot with the same `SDF_SERVICES_BMP_TEST` + `sdf_services_perm_get(...) == 3u` loop used at `sdf_services.c:1062-1068`
- [x] 1.4 Return `ESP_ERR_INVALID_STATE` when the target user's permission is admin and the admin count is one, before `fp_delete_user()` is reached
- [x] 1.5 Confirm `sdf_services_clear_all_users()` is untouched and remains exempt

## 2. CLI Message Accuracy

- [x] 2.1 Update `cmd_user_del()` (`sdf_cli_commands.c:252-258`) so the `ESP_ERR_INVALID_STATE` branch states the last-admin reason plainly rather than hedging with "may be"
- [x] 2.2 Verify the `ESP_ERR_NOT_FOUND` branch now reports a condition the function actually returns

## 3. Host Tests

- [x] 3.1 Test: deleting the sole admin is refused with `ESP_ERR_INVALID_STATE`, the cached bitmap is unchanged, and no sensor delete was issued
- [x] 3.2 Test: deleting one of two admins succeeds and the cache plus NVS persistence are updated
- [x] 3.3 Test: deleting a non-admin succeeds while exactly one admin is enrolled
- [x] 3.4 Test: deleting an unenrolled user id returns `ESP_ERR_NOT_FOUND` with no sensor delete issued
- [x] 3.5 Test: `sdf_services_clear_all_users()` still clears a single-admin device
- [x] 3.6 Register the new tests in `firmware/test_runner/main/test_runner_main.c`

## 4. Verification

- [x] 4.1 `rtk cargo`-equivalent firmware build passes (`idf.py build`) with no new warnings
- [x] 4.2 Host test runner passes, including the pre-existing `sdf_services` suite
- [x] 4.3 `openspec validate last-admin-delete-guard --strict` passes
