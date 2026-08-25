## 1. Storage: Unified User Record

- [x] 1.1 Define the unified per-user record (name, `has_credential`, salt, stretched credential) keyed by fingerprint user id 1-10, deliberately carrying no permission field
- [x] 1.2 Raise `SDF_STORAGE_WEB_USER_MAX` from 5 to 10 and reconcile it with `SDF_STORAGE_FP_USER_ID_MAX`, keeping the existing sync comment accurate
- [x] 1.3 Remove `permission` from `sdf_storage_web_user_t` (`sdf_storage.h:59`)
- [x] 1.4 Re-key `sdf_storage_web_user_save`/`load`/`clear` from table index to user id
- [x] 1.5 Fold `sdf_storage_save_user_name`/`load_user_name`/`delete_user_name` and `sdf_storage_fp_user_name_key()` into the unified record
- [x] 1.6 Update `sdf_storage_web_user_find_by_name()` to scan user ids, and to report a record with `has_credential == false` as no match
- [x] 1.7 Update `sdf_storage_web_user_count()` to count records holding a credential
- [x] 1.8 Verify `sdf_storage_erase_all()` clears the unified records, replacing its separate web-user and name clears
- [x] 1.9 Host unit tests for 1.1-1.8, including absent-key reads, a record with a name but no credential, and capacity at all ten ids

## 2. Services: Capture The Authorizing Admin

- [x] 2.1 Add the authorizing admin's user id to `sdf_services`' owned pending-request state alongside `request_web_username`/`request_web_password_hash` (`sdf_services_internal.h:65-66`)
- [x] 2.2 In `sdf_services_try_claim_admin_action()` (`sdf_services.c:455-495`), capture `match->user_id` into that state when the claimed action is `WEB_REG_AUTH`
- [x] 2.3 Extend the pending-request getter used by `sdf_app_on_web_reg_auth_result` to return the bound user id, keeping it out of any event payload
- [x] 2.4 Clear the captured user id on every path that clears the pending request (denied, timeout, non-success resolution)
- [x] 2.5 Host unit tests: user id captured on authorization, absent on denial and timeout, never present in an emitted event

## 3. Services: Registration Decision Binds And Replaces

- [x] 3.1 Extend the pure registration-decision function to take the authorizing user id and persist the credential against it
- [x] 3.2 Replace an existing credential for that user id in place, generating a fresh salt and stretched credential and retaining neither previous value
- [x] 3.3 Refuse the registration when no authorizing user id is present, persisting nothing
- [x] 3.4 Set the submitted name as that user's name as part of the same decision
- [x] 3.5 Host unit tests: first registration binds; second registration by the same admin replaces and the old credential no longer verifies; second registration by a different admin creates a separate account; unbound registration refused

## 4. Services: Name Uniqueness

- [x] 4.1 Reject a registration whose submitted name is already held by a different enrolled user
- [x] 4.2 Reject a rename whose target name is already held by a different enrolled user, leaving both names unchanged
- [x] 4.3 Confirm deletion releases the name implicitly by clearing the record, with no separate reclamation step
- [x] 4.4 Host unit tests for 4.1-4.3, including renaming a user to its own current name (must succeed as a no-op)

## 5. Services: Deletion Destroys The Bound Credential

- [x] 5.1 Extend `sdf_services_delete_user()` to clear the deleted user's unified record, including any credential, after the sensor delete and cache update succeed
- [x] 5.2 Confirm `sdf_services_clear_all_users()` clears all unified records
- [x] 5.3 Host unit tests: deleting an admin with an account destroys the credential; a `LOGIN_INIT` for that name is thereafter answered as an unknown name

## 6. Companion Service: Bound Session Authority

- [x] 6.1 Add a bound user id to the authenticated-connection record in `sdf_ble_companion`, set on successful `LOGIN_VERIFY`
- [x] 6.2 Replace the authenticated-only check guarding Config, Enrollment and OTA with a check that additionally resolves the bound user's current enrolment and permission from the cache
- [x] 6.3 Clear the bound user id on LOGOUT and on disconnect, alongside the existing authentication state
- [x] 6.4 Treat a `LOGIN_INIT` name whose record has no credential exactly as an unknown name, routing it through the existing pseudo-salt path
- [x] 6.5 Refuse to grant admin authority at login when the bound user's permission is not admin
- [x] 6.6 Host unit tests: authority re-read per request; demotion of the bound user refuses subsequent restricted access on an open connection; deletion of the bound user does the same; non-admin name is indistinguishable from an unknown name in reply shape

## 7. Web Companion App

- [x] 7.1 Present the registration name field as the user's name on the device, not a separate account username
- [x] 7.2 State on the registration form that the account will belong to the admin who confirms it with a fingerprint scan
- [x] 7.3 Present re-registration as the password-reset path, warning before submit that the confirming admin's existing credential will be replaced
- [x] 7.4 Present only Admin and Standard wherever a permission is displayed or offered; do not offer the reserved intermediate level

## 8. Documentation

- [x] 8.1 Record in `doc/features.md` that permission level 2 confers no companion access, so the open Elevated User decision is not narrowed by silence
- [x] 8.2 Update `web-companion/README.md` for the admin-bound account model and the re-registration reset path
- [x] 8.3 Update the Authentication characteristic wire-format documentation for the REGISTER name semantics, per the requirement that documentation match enforcement

## 9. Verification

- [x] 9.1 Firmware build passes (`idf.py build`) with no new warnings
- [x] 9.2 Host test runner passes, with new tests registered in `firmware/test_runner/main/test_runner_main.c`
- [ ] 9.3 Emulator verification of a full register → login → demote → refused-access sequence via `esp-emu` and the BLE companion harness in `tools/ble_ota_harness/` — **verified up to the esp-emu wedge** (`scripts/run_ble_ota_harness.sh --scenario identity`): pre-auth refusal, credential binding to the authorizing admin (`Saved web account for user_id=1`), challenge-response login, authorized access, and the fixture's demotion hook all confirmed on-device; the post-demotion refused-access observations land just past the documented esp-emu HCI pipeline wedge (~28–31 inbound ACL packets/boot, esp-emu 0.39.0–0.40.1, see `add-ble-ota-emulator-harness/design.md` D6) and are covered host-side by `test_user_is_enrolled_admin_*`. Rerun `--scenario identity` once an esp-emu release fixes the defect for full wire-level confirmation. Left unchecked to match how `add-ble-ota-emulator-harness` recorded the same blocker (its tasks 7.5, 7.6 and 8.1 stayed unchecked with annotations); the refused-access assertion this task exists for is the part that did not run.
- [x] 9.4 `openspec validate companion-identity --strict` passes
