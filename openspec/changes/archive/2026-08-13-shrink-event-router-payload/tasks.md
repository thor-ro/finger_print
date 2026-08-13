## 1. Event router type and payload changes

- [x] 1.1 Remove `SDF_EVENT_ROUTER_WEB_REG_AUTH_REQUEST` from `sdf_event_router_type_t` in `firmware/components/sdf_event_router/include/sdf_event_router.h`
- [x] 1.2 Remove `sdf_event_router_web_reg_auth_request_payload_t` and its `web_reg_auth_request` union member
- [x] 1.3 Shrink `sdf_event_router_web_reg_auth_result_payload_t` to `{ bool authorized; }` (drop `username` and `permission`)
- [x] 1.4 Verify (e.g. via a scratch `sizeof()` check or comment) that the union's largest remaining member is `sdf_event_router_audit_payload_t` (16 bytes) and `sizeof(sdf_event_router_event_t)` has dropped from ~76 to ~28 bytes

## 2. Wire WEB_REG_AUTH request directly from the BLE task

- [x] 2.1 In `firmware/components/sdf_app/src/sdf_app.c`, change `sdf_ble_companion_on_auth_request()` to call `sdf_services_set_web_reg_auth(username, password_hash, hash_len)` followed by `sdf_services_request_admin_action(SDF_SERVICES_ADMIN_ACTION_WEB_REG_AUTH)` directly, instead of building and emitting a `SDF_EVENT_ROUTER_WEB_REG_AUTH_REQUEST` event — mirroring `sdf_app_on_ble_admin_action_request()`'s existing direct-call pattern for NUKI_REPAIR/ENROLL_ADMIN/ZB_JOIN
- [x] 2.2 Preserve today's failure semantics: if either call returns non-`ESP_OK`, log and drop (no new error surfaced to the BLE client)
- [x] 2.3 Remove the `SDF_EVENT_ROUTER_WEB_REG_AUTH_REQUEST` subscription (in the subscriber setup block) and delete the now-dead `sdf_app_on_web_reg_auth_request()` handler and its `case` in the event dispatch switch

## 3. Stop round-tripping RESULT payload fields

- [x] 3.1 In `sdf_app_on_web_reg_auth_result()`, replace reads of `event->payload.web_reg_auth_result.username` and `.permission` with a call to the existing `sdf_services_get_web_reg_auth()` accessor, called before `sdf_services_clear_web_reg_auth()` at the end of the function (unchanged ordering)
- [x] 3.2 Keep reading `event->payload.web_reg_auth_result.authorized` from the (now-shrunk) event payload as-is

## 4. Verification

- [x] 4.1 Repo-wide grep for `SDF_EVENT_ROUTER_WEB_REG_AUTH_REQUEST`, `web_reg_auth_request`, and `sdf_app_on_web_reg_auth_request` to confirm no remaining references outside the removed code
- [x] 4.2 Build the firmware (or the Linux host `test_runner` target) and confirm no compile errors from the removed type/payload
- [x] 4.3 Run the full test suite (`sdf_test_runner`), including `test_web_auth_should_resolve_on_web_reg_auth_failure` and `test_web_auth_should_not_resolve_on_web_reg_auth_success`, and confirm all tests still pass unchanged
- [x] 4.4 Manually trace (or exercise via emulator/hardware) one full Web Registration Authorization cycle end-to-end (BLE REGISTER write → Admin fingerprint scan → BLE reply) to confirm behavior is unchanged
