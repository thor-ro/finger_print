## 1. Button dispatch cleanup

- [ ] 1.1 Remove the `BUTTON_MULTIPLE_CLICK` (Triple-click) → `SDF_SERVICES_ADMIN_ACTION_ENROLL_ADMIN` registration in `sdf_services_button.c`.
- [ ] 1.2 Remove the `BUTTON_LONG_PRESS_START` @ 3000ms (Hold-3s) → `SDF_SERVICES_ADMIN_ACTION_ZB_JOIN` registration in `sdf_services_button.c`.
- [ ] 1.3 Simplify `sdf_button_cb()`'s pre-enrollment bootstrap branch (`sdf_services_button.c:125-143`) so it only special-cases `action == SDF_SERVICES_ADMIN_ACTION_ENROLL`, since `ENROLL_ADMIN` is no longer button-reachable.
- [ ] 1.4 Confirm (by inspection/test) that triple-click and Hold-3s now produce no action and leave `pending_admin_action` unaffected.

## 2. Shared pending-action connection-routing plumbing

- [ ] 2.1 Add an optional "originating BLE connection handle" field to the pending-admin-action state in `sdf_services_admin.c` (or equivalent shared location), unset for button-originated requests.
- [ ] 2.2 On pending-action resolution (approved/denied/timeout), if a connection handle is present, route the result back to that connection via the existing GATT notification mechanism used by `WEB_REG_AUTH`; leave button-originated behavior (LED feedback only) unchanged when absent.
- [ ] 2.3 Ensure this shared mechanism guarantees resolution even on non-success outcomes, per the "Pending BLE-Originated Admin Actions Always Resolve" spec requirement — reuse rather than duplicate the existing `WEB_REG_AUTH` "always resolves" logic.
- [ ] 2.4 If `nuki-pairing-setup-flow`'s `NUKI_REPAIR` connection-routing has already landed by this point, refactor it to use this shared mechanism instead of maintaining a separate implementation.

## 3. BLE-triggered Enroll-Admin action

- [ ] 3.1 Add a new BLE Companion Service write op for an authenticated client to request `ENROLL_ADMIN` (mirroring the existing Web Registration / Nuki re-pair request paths in `sdf_ble_companion.c`).
- [ ] 3.2 Add a request callback that tracks the originating connection handle and routes the request into the pending-admin-action flow using the existing `ENROLL_ADMIN` action id.
- [ ] 3.3 Reject requests from unauthenticated (not logged-in) BLE clients before entering the pending state.
- [ ] 3.4 Confirm `sdf_app_on_admin_action()`'s existing `ENROLL_ADMIN` case body (start local enrollment with permission=3) is invoked unchanged on authorization.

## 4. BLE-triggered Zigbee Join action

- [ ] 4.1 Add a new BLE Companion Service write op for an authenticated client to request `ZB_JOIN`.
- [ ] 4.2 Add a request callback that tracks the originating connection handle and routes the request into the pending-admin-action flow using the existing `ZB_JOIN` action id.
- [ ] 4.3 Reject requests from unauthenticated (not logged-in) BLE clients before entering the pending state.
- [ ] 4.4 Confirm `sdf_app_on_admin_action()`'s existing `ZB_JOIN` case body (`sdf_protocol_zigbee_permit_join()`) is invoked unchanged on authorization.

## 5. Companion app (web-companion)

- [ ] 5.1 Add a UI affordance (post-login, admin-only) to request enrollment of a new admin, showing pending/waiting state and reflecting approval/denial/timeout.
- [ ] 5.2 Add a UI affordance (post-login, admin-only) to request a Zigbee join window, showing pending/waiting state and reflecting approval/denial/timeout.
- [ ] 5.3 Reuse whatever shared "pending BLE admin action" UI pattern `nuki-pairing-setup-flow` establishes for its Nuki re-pair trigger, rather than building a third bespoke pattern.

## 6. Documentation updates

- [ ] 6.1 Update `doc/First Time Flow Concept.md` to remove Triple-click and Hold-3s from the Configuration Button Mapping Summary table and describe the companion-app-triggered flow instead.
- [ ] 6.2 Update `doc/user_manual.md` wherever it references Triple-click (enroll admin) or Hold-3s (Zigbee join), documenting the companion-app flow as the only path.

## 7. Verification

- [ ] 7.1 Manual/emulator test: triple-click and Hold-3s produce no action at any setup stage.
- [ ] 7.2 Manual/emulator test: authenticated companion client requests Enroll-Admin → pending state, admin fingerprint required, new-admin enrollment starts on approval, result routed back to the requesting client.
- [ ] 7.3 Manual/emulator test: authenticated companion client requests Zigbee join → pending state, admin fingerprint required, join window opens on approval, result routed back to the requesting client.
- [ ] 7.4 Manual/emulator test: both new triggers rejected outright when the requesting connection isn't authenticated.
- [ ] 7.5 Manual/emulator test: denial/timeout path notifies the requesting client for both new triggers (always resolves).
- [ ] 7.6 Manual/emulator test: unclaimed device, single-click still enrolls immediately without requiring admin authorization (bootstrap branch simplification didn't regress this).
