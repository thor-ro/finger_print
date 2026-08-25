## 1. Storage Foundations

- [x] 1.1 Add setup-completion latch accessors to `sdf_storage` (`save`/`load`/`clear`), following the existing "absent key reads as unset, not an error" convention
- [x] 1.2 Add admission-record accessors to `sdf_storage` (`add`/`remove`/`load_all`/`clear_all`) storing addr type plus 6-byte address, sized to at least `CONFIG_BT_NIMBLE_MAX_BONDS`
- [x] 1.3 Extend `sdf_storage_erase_all()` to clear the latch and all admission records
- [x] 1.4 Host unit tests for 1.1–1.3, including absent-key reads and capacity exhaustion

## 2. Setup-Phase Lifecycle

- [x] 2.1 Introduce the setup-phase armed/disarmed state and its compile-time timer constants: `SDF_SETUP_ARM_WINDOW_MS` (300000), `SDF_SETUP_DEADLINE_MS` (600000), `SDF_SETUP_CONN_IDLE_MS` (120000)
- [x] 2.2 Make `sdf_services_get_setup_state()` report completion from the latch instead of deriving it from enrolled-user count plus `sdf_storage_nuki_load()`; keep the intermediate Admin-enrolled, account-registered and Nuki-paired states derived
- [x] 2.3 Arm the setup phase on boot when the latch is unset and no prior arm/disarm state exists, and on factory-reset completion
- [x] 2.4 Implement the arm window from arm time and the setup deadline from the first accepted connection, neither extended by client activity, progress, disconnection, or reconnection
- [x] 2.5 Implement the connection idle timer: terminate the silent setup connection and re-arm advertising, leaving the setup deadline and all partial state untouched
- [x] 2.6 Implement the timeout wipe on arm-window or setup-deadline expiry: enrolled templates, web accounts, bonds, admission records, partial Nuki credentials — with template-erase failure logged and non-fatal
- [x] 2.7 Disarm the setup phase and stop advertising on arm-window or setup-deadline expiry
- [x] 2.8 Bind the setup-phase button press to reclaim-and-re-arm: terminate the current setup connection, re-arm advertising, restart both the arm window and the setup deadline, set no pending admin action
- [x] 2.9 Host unit tests for arm/disarm transitions, both timers' start conditions and non-extendability, idle-timer isolation from the deadline, button restart of both timers, wipe coverage, and latch irreversibility under user/credential deletion

## 3. Factory Reset Without Fingerprint Gate

- [x] 3.1 Remove the pending-admin-action path for `SDF_SERVICES_ADMIN_ACTION_FACTORY_RESET` so the gesture executes directly
- [x] 3.2 Ensure factory reset clears the latch, arms the setup phase, and erases all state listed in the `device-setup-phase` spec
- [x] 3.3 Host unit tests: reset proceeds with zero readable admins, sets no pending action, leaves the device re-claimable

## 4. Retire Button-Driven Setup

- [x] 4.1 Remove `sdf_button_resolve_single_click_action()` and its single-click setup-action dispatch
- [x] 4.2 Remove `sdf_services_try_bootstrap_admin_action()` and the `sdf_services_admin_origin_t` plumbing that exists only to feed it
- [x] 4.3 Scope the double-click pairing-window binding to the latch being set
- [x] 4.4 Remove or update the host tests covering the removed requirements (`test_setup_state_unclaimed_when_no_enrolled_users`, `test_button_single_click_resolves_to_enroll_on_unclaimed_device`, and the bootstrap-bypass tests) and deregister them from `test_runner_main.c`

## 5. Advertising Modes And Connection Cap

- [x] 5.1 Extend `sdf_ble_companion_restart_advertising()` to select among sparse-filtered, unfiltered-setup, pairing-window, and not-advertising based on latch and armed state
- [x] 5.2 Add an unfiltered, connectable setup-phase advertising mode
- [x] 5.3 Enforce the single-connection cap in `BLE_GAP_EVENT_CONNECT` while the latch is unset: terminate any second inbound connection
- [x] 5.4 Restore the ordinary connection limit once the latch is set
- [x] 5.5 Host unit tests for mode selection across latch/armed combinations and for second-connection rejection

## 6. Admission Records And Allow-List Seeding

- [x] 6.1 Rewrite `sdf_ble_companion_seed_allow_list()` to seed the intersection of admission records and `ble_store_util_bonded_peers()`
- [x] 6.2 Write an admission record when a device is admitted through the pairing window (`BLE_GAP_EVENT_ENC_CHANGE` admit path)
- [x] 6.3 Clear the admission record alongside the bond and allow-list entry on failed-login eviction
- [x] 6.4 Host unit tests: abandoned setup bond is not seeded, admitted-and-bonded peer is seeded, admission without a bond grants nothing, eviction clears both records

## 7. Setup-State Characteristic

- [x] 7.1 Add a read-only setup-state characteristic to the Companion GATT database, readable on an encrypted but unauthenticated link
- [x] 7.2 Report the enumeration: setup not started, Admin enrolled, Nuki paired, setup complete
- [x] 7.3 Verify the persisted CCCD capacity requirement still holds if the characteristic is made NOTIFY-capable, or leave it read-only
- [x] 7.4 Document the characteristic's wire format alongside the existing Auth/Config/Enroll/OTA formats
- [x] 7.5 Host unit tests: readable pre-login, restricted characteristics still return insufficient authentication

## 8. Setup Completion Path

- [x] 8.1 Add the explicit setup-completion command on the Companion Service, accepted only on an authenticated session
- [x] 8.2 Reject completion when prerequisites are unmet and report which step is outstanding
- [x] 8.3 Implement the completion sequence in `sdf_app` in order: persist admission record, persist latch, populate allow list, push to controller, switch advertising, restore connection limit
- [x] 8.4 Order registration behind Admin enrolment so `WEB_REG_AUTH` always has an enrolled Admin to authorize against
- [x] 8.5 Host unit tests including the crash-safety ordering: interruption after admission and before latch leaves the device in the setup phase

## 9. Web Companion Wizard

- [x] 9.1 Add a wizard view preceding connection/auth/dashboard, entered when the device reports it is not setup-complete
- [x] 9.2 Read setup state before login and resume at the reported step
- [x] 9.3 Implement the step sequence: Admin enrolment, account registration, Nuki pairing, explicit completion
- [x] 9.4 Gate the registration form on an Admin being enrolled
- [x] 9.5 Disclose the setup time bound up front and report a lapse when the device disconnects and stops advertising without completing
- [x] 9.6 Report completion, including that the device is now locked to this browser

## 10. Verification

- [x] 10.1 Run the host test suite and confirm all new and updated tests pass
- [x] 10.2 Verify under `esp-emu`: fresh boot enters the setup phase and advertises unfiltered
- [ ] 10.3 Verify under `esp-emu`: second connection during setup is terminated
- [ ] 10.4 Verify under `esp-emu`: arm-window expiry with no client stops advertising, and a button press re-arms
- [ ] 10.5 Verify under `esp-emu`: setup-deadline expiry mid-wizard wipes state and stops advertising, and reconnecting does not extend the deadline
- [ ] 10.6 Verify under `esp-emu`: completion switches to filtered advertising and the admitted peer reconnects after reboot
- [ ] 10.7 Verify under `esp-emu`: a bond made during an abandoned setup phase is not allow-listed after reboot

## 11. Documentation

- [x] 11.1 Rewrite `doc/First Time Flow Concept.md` for the app-guided flow, including the setup phase, timeout, and button semantics
- [x] 11.2 Update `doc/user_manual.md` for the wizard, the re-arm press, and factory reset without a fingerprint
- [x] 11.3 Resolve the Double-Press drift in both documents against the scoped binding from task 4.3
- [x] 11.4 Note the factory-reset exposure and recommend interior button placement in the user manual
- [x] 11.5 Update `web-companion/README.md` for the wizard flow
