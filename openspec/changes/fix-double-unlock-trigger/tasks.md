## 1. Remove unlock_cb from sdf_services

- [ ] 1.1 In `sdf_services.h` (or `sdf_services_internal.h`), remove `unlock_cb` and `unlock_ctx` fields from `sdf_services_config_t`
- [ ] 1.2 In `sdf_services.c`, remove those fields from `sdf_services_get_default_config()`
- [ ] 1.3 In `sdf_services_match.c`, remove the local `unlock_cb`/`unlock_ctx` copies and the `unlock_cb(unlock_ctx, match.user_id)` call
- [ ] 1.4 Confirm the event emission `SDF_EVENT_ROUTER_BIOMETRIC_MATCH` remains intact after the admin-claim check

## 2. Update sdf_app

- [ ] 2.1 In `sdf_app.c`, remove the `sdf_app_on_fingerprint_unlock()` static function
- [ ] 2.2 In `sdf_app_init()`, remove the `services_cfg.unlock_cb` and `services_cfg.unlock_ctx` assignments
- [ ] 2.3 Confirm `sdf_app_on_event()` case `SDF_EVENT_ROUTER_BIOMETRIC_MATCH` still calls `sdf_app_lock_action(SDF_LOCK_ACTION_UNLATCH, 0)`

## 3. Update Tests

- [ ] 3.1 Search for `unlock_cb` in test files; update any test that relied on the callback to instead subscribe to `SDF_EVENT_ROUTER_BIOMETRIC_MATCH`

## 4. Build & Verify

- [ ] 4.1 Build firmware, confirm no compile errors
- [ ] 4.2 Confirm no remaining references to `unlock_cb` or `unlock_ctx` in source
