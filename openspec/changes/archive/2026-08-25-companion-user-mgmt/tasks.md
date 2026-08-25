## 1. Services: Distinguishable Outcomes

- [x] 1.1 Define the user-management outcome enumeration (`ok`, `not_found`, `id_occupied`, `last_admin`, `name_taken`, `busy`, `denied`, `timeout`, `invalid`) in `sdf_services`' public header, as the reported result of a user-management verb rather than a decoding of `esp_err_t`
- [x] 1.2 Report `sdf_services_delete_user()`'s last-admin refusal distinctly from its uninitialised-services and sensor-failure cases (`sdf_services.c`, the guard added by `last-admin-delete-guard`)
- [x] 1.3 Report `sdf_services_change_user_permission()`'s last-admin refusal distinctly from its busy and timeout cases (`sdf_services.c:1143-1189`)
- [x] 1.4 Report `sdf_services_set_user_name()`'s duplicate-name refusal distinctly from its other failures
- [x] 1.5 Report an enrolment request against an already-enrolled user id as `id_occupied`, moving the check off the CLI (`sdf_cli_commands.c:317-332`) so every caller gets it
- [x] 1.6 Render the same enumeration from the CLI, preserving its current messages, and delete the context-guessing comment at `sdf_cli_commands.c:279-283` that only existed because the code could not tell
- [x] 1.7 Host unit tests: each outcome is produced by the condition that names it, and no two conditions produce the same one

## 2. Services: Deletion And Enrolment Join The Admin Gate

- [x] 2.1 Add a delete-user admin action kind alongside the existing `SDF_SERVICES_ADMIN_ACTION_*` set, carrying the target user id
- [x] 2.2 Extend `sdf_services_pulse_pending_action_led()` for it, keeping the mapping exhaustive per `sdf-services-tasks — Pending Admin Action LED Mapping Is Complete`
- [x] 2.3 Execute the delete on authorization in `sdf_services_execute_admin_action()`, applying the last-admin guard before any sensor operation, and resolve the action with an outcome
- [x] 2.4 Add an authorized-entry enrolment path so a companion enrolment arms the admin gate first and starts the enrolment state machine only on a matched admin scan
- [x] 2.5 Leave `sdf_services_request_enrollment()`'s existing button and setup-phase callers working unchanged, and confirm no caller reaches the enrolment state machine from BLE without an authorizing scan
- [x] 2.6 Resolve every new action on denial and on timeout, per `sdf-services-tasks — Pending BLE-Originated Admin Actions Always Resolve`
- [x] 2.7 Host unit tests: delete authorized, denied, timed out, refused as last admin before the gate is armed; companion enrolment refused without a matching admin scan; a second request while one is in flight answers `busy`

## 3. Services: One User-List Serializer

- [x] 3.1 Extract the user-list JSON producer from `sdf_app_update_zigbee_user_list()` (`sdf_app.c:1389-1447`) into a single function used by both the Zigbee report and the companion reply
- [x] 3.2 Keep the Zigbee size check against `SDF_ZIGBEE_USER_LIST_MAX` at its existing call site
- [x] 3.3 Host unit tests: the shape is unchanged for the Zigbee caller; a user with no name omits the field rather than emitting an empty string

## 4. Companion Service: Request/Reply Protocol

- [x] 4.1 Parse a verb and a client-supplied request id in `sdf_ble_companion_dispatch_enroll_write()`, replacing the opaque forward of the whole payload
- [x] 4.2 Reply exactly once to every request, including requests rejected for malformed JSON, an unknown verb, or an out-of-range field
- [x] 4.3 Carry the originating request id on enrolment progress notifications so they are attributable to the request that started them
- [x] 4.4 Dispatch every verb to the `sdf_app` task and return from the GATT callback without waiting, so no user-management verb blocks the NimBLE host task
- [x] 4.5 Notify the terminal reply when the admin action resolves, reusing the resolution path `sdf_ble_companion_reply_admin_action()` already drives
- [x] 4.6 Chunk the list reply across notifications, with an explicit final marker so a truncated list is not indistinguishable from a complete one
- [x] 4.7 Refuse a second in-flight request from the same connection with `busy` rather than queueing or dropping it
- [x] 4.8 Host unit tests for 4.1-4.7, including a malformed request answered rather than dropped, and a list that needs more than one notification

## 5. Companion Service: Setup-Phase Admission

- [x] 5.1 Admit the enrolment verb on a setup-phase connection to a device with no enrolled users, alongside the existing live-admin-authority case in `sdf_ble_companion_enroll_access()` (`sdf_ble_companion.c:864-868`)
- [x] 5.2 Refuse every other verb on that connection, and refuse the enrolment verb too once any user is enrolled
- [ ] 5.3 Confirm the first-time setup wizard's Admin-enrolment step now succeeds, and that it fails on `main` today for the reason the proposal states
      - PARTIALLY CONFIRMED: the admission rule is implemented (`sdf_ble_companion_um_admits()`) and host-unit-tested (admit enrol-only with no users, refuse once any user exists, refuse every other verb); the pre-change failure mode (enrolment write refused with INSUFFICIENT_AUTHEN during wizard step 1 because no admin can exist yet) follows from `main`'s `conn_has_admin_authority()` gate. End-to-end wizard confirmation needs a browser/WebBLE session against a wiped device and has not been run.
- [x] 5.4 Host unit tests: enrolment admitted with no users enrolled during setup; refused once one user exists; every other verb refused on the same connection; all verbs refused outside the setup phase without admin authority

## 6. Web Companion App

- [x] 6.1 Add a user-management view listing enrolled users with id, name and permission, sourced from the list verb
- [x] 6.2 Offer enrol, delete, permission change and rename, each stating up front that an admin fingerprint scan is required and how many enrolment scans follow
- [x] 6.3 Render each refusal reason specifically — last admin, name taken, id occupied, busy, denied, timed out — rather than a generic failure
- [x] 6.4 Warn before an admin submits a change that demotes or deletes their own user, explaining that their session will lose authority
- [x] 6.5 Correlate replies by request id, replacing the single-slot pending-action workaround for this characteristic (`web-companion/app.js:651-672`)
- [x] 6.6 Update the wizard's Admin-enrolment step to the new request shape
- [x] 6.7 Continue to offer only Admin and Standard where a permission is chosen, per `companion-identity`

## 7. Documentation

- [x] 7.1 Document the Enrollment characteristic's per-verb wire format in `doc/sdf_sas.md`, alongside the Authentication characteristic's, including the outcome enumeration
- [x] 7.2 Document companion user management in `doc/user_manual.md`, including the scan each verb requires
- [x] 7.3 Correct `doc/user_manual.md:216-222` where it claims `user add` and `user del` require an Admin fingerprint: `user add` arms the enrolment directly and `user del` performs it directly, neither through the admin gate
- [x] 7.4 Correct the CLI's own "Scan an admin fingerprint to authorize enrollment" prompt (`sdf_cli_commands.c:334-336`) to describe what actually happens next

## 8. Verification

- [x] 8.1 `idf.py build` for esp32c6 clean, no new warnings
- [x] 8.2 Full host suite green, with the new tests registered in `test_runner_main.c`
- [x] 8.3 `openspec validate companion-user-mgmt --strict`
- [x] 8.4 Emulator scenario driving list, enrol, delete, permission change and rename over BLE, including a refused last-admin delete and a `busy` reply, using `scripts/run_ble_ota_harness.sh`; record what the esp-emu ACL wedge (`add-ble-ota-emulator-harness` design D6) prevents confirming rather than ticking past it
      - OUTCOME: new `--scenario user-mgmt` in `scripts/run_ble_ota_harness.sh` drives all five verbs against the ble_ota_gate fixture under esp-emu. Confirmed client-side: pre-auth UM write refused with INSUFFICIENT_AUTHEN; login then `list` reply with explicit end marker; last-admin delete refused with the named `last_admin` outcome before any scan. The esp-emu ACL wedge (D6: inbound HCI ACL packets silently dropped, esp-emu 0.40.1) deterministically swallows terminal-reply notifications from the third post-login notification onwards: the gated rename/set_permission/enroll replies are emitted by the device (logged: "UM request 102 resolved: ok", action 12 claimed by the synthetic admin match, enrolment started) but never reach the harness client, and a liveness probe confirms the pipe itself is wedged. The harness reports this as `rename_gated=WEDGE_EMU` rather than PASS or a generic failure; those three deliveries are covered instead by the host unit tests (sections 1, 2, 4) and the emulated device-side logs.
