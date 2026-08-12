## 1. Button dispatch refactor

- [x] 1.1 Remove the `BUTTON_DOUBLE_CLICK` → `SDF_SERVICES_ADMIN_ACTION_NUKI_PAIR` registration in `sdf_services_button.c`.
- [x] 1.2 Add a helper (e.g. `sdf_services_setup_state()` or similar) that returns whether the device is unclaimed, claimed-with-setup-incomplete, or claimed-with-setup-complete, backed by `enrolled_user_count` and `sdf_storage_nuki_load()` (or the already-loaded in-RAM Nuki credential state if cheaper at the call site).
- [x] 1.3 Update the `BUTTON_SINGLE_CLICK` callback registration/dispatch in `sdf_services_button.c` to resolve its action dynamically at press time using the helper from 1.2, per the `sdf-services-tasks` delta spec's "State-Dependent Single-Click Setup Action" requirement.
- [x] 1.4 Verify the existing pending-admin-action gate in `sdf_button_cb()` still applies correctly to the `NUKI_PAIR` case reached via single-click (an admin necessarily exists in that branch already, so no new gating logic should be needed — confirm this with a code read, not just assumption).
- [x] 1.5 Confirm (by inspection/test) that a double-click after this change produces no action and leaves `pending_admin_action` unaffected.

## 2. BLE-triggered Nuki re-pair action

- [x] 2.1 Add `SDF_SERVICES_ADMIN_ACTION_NUKI_REPAIR` to the admin-action enum (`sdf_services_internal.h` or equivalent) alongside the existing `NUKI_PAIR`/`WEB_REG_AUTH`/etc.
- [x] 2.2 Add a new BLE Companion Service write op for an authenticated client to request Nuki re-pairing (mirroring the existing Web Registration request path in `sdf_ble_companion.c`), including a check that setup is already complete before accepting the request (reject otherwise, per spec).
- [x] 2.3 Add an `on_nuki_repair_request` callback (parallel to `on_auth_request`) that tracks the originating connection handle and routes the request into the pending-admin-action flow.
- [x] 2.4 Wire `sdf_services_admin.c` / `sdf_admin_task` to accept `NUKI_REPAIR` as a pending action type (only set if `pending_admin_action == NONE`, per existing pattern), with its own LED pulse color distinct from `NUKI_PAIR`/`WEB_REG_AUTH`.
- [x] 2.5 In `sdf_app.c`, add an admin-action-result handler for `NUKI_REPAIR` that, on approval, invokes the same underlying `sdf_nuki_ble_set_enabled(&s_ble, true)` / `sdf_nuki_ble_start(&s_ble)` calls the existing `NUKI_PAIR` case uses.
- [x] 2.6 Route the approval/denial/timeout result back to the originating BLE connection (reusing or extending the `WEB_REG_AUTH` result-routing plumbing at `sdf_app_on_web_reg_auth_result()` as a template), guaranteeing the pending request always resolves per the "Pending re-pair request always resolves" scenario.
- [x] 2.7 Reject requests from unauthenticated (not logged-in) BLE clients before entering the pending state.

## 3. Companion app (web-companion)

- [x] 3.1 Add a UI affordance (post-login, once setup-complete state is known) to trigger a Nuki re-pair request over BLE.
- [x] 3.2 Show pending/waiting state ("scan admin fingerprint on the device") while the request is outstanding, and reflect approval/denial/timeout when the result arrives.

## 4. Documentation updates

- [x] 4.1 Update `doc/First Time Flow Concept.md` to describe the new sequential single-click flow (enroll admin, then single-click again to pair Nuki) in place of the Double-Press description, and update the Configuration Button Mapping Summary table.
- [x] 4.2 Update `doc/user_manual.md` wherever it references Double-Press for Nuki pairing, and document the new BLE-triggered re-pair trigger as the post-setup path (alongside factory reset).

## 5. Verification

- [ ] 5.1 Manual/emulator test: unclaimed device, single-click → admin enrollment starts.
- [ ] 5.2 Manual/emulator test: claimed device, Nuki not yet paired, single-click → pending `NUKI_PAIR`, admin fingerprint scan required, pairing starts on approval.
- [ ] 5.3 Manual/emulator test: claimed device, Nuki already paired, single-click → standard user enrollment (not Nuki pairing).
- [ ] 5.4 Manual/emulator test: double-click at any setup stage → no action.
- [ ] 5.5 Manual/emulator test: factory reset clears Nuki credentials and re-opens the single-click-pairs-Nuki window on next admin enrollment.
- [ ] 5.6 Manual/emulator test: BLE Companion re-pair request after setup complete → pending state, admin fingerprint required, result routed back to the requesting client; timeout/denial path also notifies the client.
- [ ] 5.7 Manual/emulator test: BLE Companion re-pair request rejected when setup is not yet complete, and rejected when the requesting connection isn't authenticated.
