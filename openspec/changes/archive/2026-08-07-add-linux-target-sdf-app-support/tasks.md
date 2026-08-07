## 1. sdf_services Scaffolding

- [x] 1.1 Add `PRIV_REQUIRES mbedtls` to `firmware/components/sdf_services/CMakeLists.txt`
- [x] 1.2 Confirm `sdf_services` still builds cleanly for `IDF_TARGET=linux` (`cd firmware/test_runner && idf.py build`) and for `esp32c6` (`cd firmware && idf.py build`) with no other change yet
- [x] 1.3 Create `firmware/components/sdf_services/src/sdf_services_web_auth.c` and add it to the `SRCS` list in `firmware/components/sdf_services/CMakeLists.txt`

## 2. Decision Functions

- [x] 2.1 Implement `sdf_services_web_auth_verify_login(const sdf_storage_web_user_t *user, const uint8_t *submitted_hash, size_t hash_len)` wrapping `mbedtls_ct_memcmp`, declared in `sdf_services.h`
- [x] 2.2 Implement `sdf_services_web_auth_registration_decision_t` and `sdf_services_web_auth_decide_registration(...)`, declared in `sdf_services.h`
- [x] 2.3 Implement `sdf_services_web_auth_should_resolve_on_action_complete(sdf_services_admin_action_t action, esp_err_t result)`, declared in `sdf_services.h`

## 3. Unit Tests (linux, via test_runner)

- [x] 3.1 Add login-verification cases to `firmware/components/sdf_services/test/test_sdf_services.c`: matching hash → valid, mismatched hash → invalid, wrong `hash_len` → invalid, all-zero hash against a real stored hash → invalid
- [x] 3.2 Add registration-decision cases: `admin_authorized == true` → `should_persist == true` with correctly populated `sdf_storage_web_user_t` (username, hash, permission, `valid == true`) and `reply_authorized == true`; `admin_authorized == false` → `should_persist == false` and `reply_authorized == false`
- [x] 3.3 Add resolve-guard cases: `sdf_services_web_auth_should_resolve_on_action_complete()` returns `true` only for `(SDF_SERVICES_ADMIN_ACTION_WEB_REG_AUTH, <non-ESP_OK>)`, and `false` for every other `sdf_services_admin_action_t` value and for `(WEB_REG_AUTH, ESP_OK)`
- [x] 3.4 Wire all new test functions into `firmware/test_runner/main/CMakeLists.txt` (already includes `test_sdf_services.c` in `SRCS`, no path change needed) and add `extern`/`RUN_TEST()` entries to `firmware/test_runner/main/test_runner_main.c`
- [x] 3.5 Run `./build/sdf_test_runner.elf` from `firmware/test_runner` and confirm all new cases pass

## 4. Wire Callers

- [x] 4.1 Add `#include "sdf_services.h"` to `firmware/components/sdf_ble_companion/src/sdf_ble_companion.c`
- [x] 4.2 Update `sdf_ble_companion_auth_access()`'s `LOGIN` branch to call `sdf_services_web_auth_verify_login()` instead of inlining `mbedtls_ct_memcmp`; confirm the rejected-login path (state reset, `BLE_ATT_ERR_INSUFFICIENT_AUTHEN`) is otherwise unchanged
- [x] 4.3 Add `#include "sdf_services.h"` to `firmware/components/sdf_app/src/sdf_app.c` if not already present
- [x] 4.4 Update `sdf_app_on_web_reg_auth_result()` to call `sdf_services_web_auth_decide_registration()`, then perform only the I/O (slot-selection loop unchanged, `sdf_storage_web_user_save()`, `sdf_ble_companion_reply_auth()`) based on the returned decision
- [x] 4.5 Update `sdf_app_on_admin_action_complete()` to call `sdf_services_web_auth_should_resolve_on_action_complete()` as its guard condition instead of the current inline `action != WEB_REG_AUTH || result == ESP_OK` check

## 5. Hardware Verification (not linux-testable)

- [x] 5.1 Flash hardware target (`sdkconfig.hw.defaults`, `esp32c6`) with the changes
- [x] 5.2 Verify LOGIN with correct password succeeds and connection reaches authenticated state
- [x] 5.3 Verify LOGIN with incorrect password is rejected and connection state resets as before
- [x] 5.4 Verify REGISTER with admin fingerprint approval persists the new web user and replies authorized
- [x] 5.5 Verify REGISTER with non-admin/rejected fingerprint denies and does not persist a user
- [x] 5.6 Verify REGISTER with no fingerprint presented (timeout) denies, resolves the pending request, and does not leave a subsequent registration attempt permanently blocked

## 6. Documentation

- [x] 6.1 No `AGENTS.md` changes expected (component structure list already describes `sdf_services` generically); confirm nothing there needs updating once implementation is final
