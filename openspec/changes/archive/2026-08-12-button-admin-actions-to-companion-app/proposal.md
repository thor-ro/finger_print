## Why

Two of the physical button's remaining gestures — Triple-click (`SDF_SERVICES_ADMIN_ACTION_ENROLL_ADMIN`) and Hold-3s (`SDF_SERVICES_ADMIN_ACTION_ZB_JOIN`) — trigger rare, administrative actions (enrolling an additional admin, opening a Zigbee join window) that don't need a dedicated physical gesture: both already require an Admin fingerprint scan to authorize once the device is claimed (via the existing `pending_admin_action` flow), so the button press itself adds no security value beyond being *a* way to originate the request. The Companion Service already has an established pattern for "an authenticated request originates over BLE, then an Admin fingerprint scan on the device authorizes it, then the result is routed back to the requester" — used today for Web Registration Authorization, and about to be used for Nuki re-pairing (`nuki-pairing-setup-flow`). Moving these two actions' *origin* from the button to the companion app frees two more gestures from an interface that was already crowded, and gives admins a richer UI for choices that benefit from one (e.g. picking who's being promoted to admin, confirming a Zigbee join window) instead of a bare timed hold.

As a side effect, this closes a gap `nuki-pairing-setup-flow`'s design explicitly deferred: `sdf_button_cb()`'s pre-enrollment bootstrap path (`sdf_services_button.c:125-143`) executes `ZB_JOIN` completely unauthenticated when `enrolled_user_count == 0`, since there's no admin yet to gate it. Removing `ZB_JOIN`'s button binding entirely removes that unauthenticated pre-enrollment path along with it. (`ENROLL_ADMIN`'s pre-enrollment behavior is already identical to `ENROLL` — both call `sdf_services_request_enrollment(1, 3)` — so removing its button binding is a simplification, not a security fix.)

## What Changes

- The `BUTTON_MULTIPLE_CLICK` (Triple-click) → `SDF_SERVICES_ADMIN_ACTION_ENROLL_ADMIN` and `BUTTON_LONG_PRESS_START` @ 3000ms (Hold-3s) → `SDF_SERVICES_ADMIN_ACTION_ZB_JOIN` registrations are removed from `sdf_services_button.c`. Neither gesture is reassigned; both are freed.
- The pre-enrollment bootstrap special-case in `sdf_button_cb()` (`action == ENROLL || action == ENROLL_ADMIN` → immediate enrollment) is simplified: since `ENROLL_ADMIN` is no longer button-reachable, the check collapses to `action == ENROLL`.
- Two new BLE Companion Service write ops let an already-authenticated (logged-in) admin client request `ENROLL_ADMIN` and `ZB_JOIN`, each entering the existing `pending_admin_action` flow and requiring an on-device Admin fingerprint scan to authorize — the same two-step guarantee the button path already provided, just with the request now originating over BLE instead of a physical gesture.
- Both new triggers reuse the *existing* `SDF_SERVICES_ADMIN_ACTION_ENROLL_ADMIN` / `SDF_SERVICES_ADMIN_ACTION_ZB_JOIN` action ids (unlike `nuki-pairing-setup-flow`'s `NUKI_REPAIR`, which needed a distinct id because `NUKI_PAIR` remained separately reachable via button in a different device state — here the button path is being removed entirely, so there's no ambiguity to disambiguate). Connection-tracking and result-routing (approved/denied/timeout, always resolves) are added on top, following the `WEB_REG_AUTH`/`NUKI_REPAIR` precedent, since a BLE-originated request needs a reply and a button press never did.
- **BREAKING**: after this change, enrolling an additional admin and opening a Zigbee join window are no longer reachable from the physical button under any circumstance. Both are companion-app-only, requiring both an authenticated companion session and a physical Admin fingerprint scan.
- Out of scope: Single-click (state-dependent per `nuki-pairing-setup-flow`) and Hold-8s (`FACTORY_RESET`) are unchanged. Factory reset remains physical-button-only by design — it's a destructive/recovery action where requiring physical device presence is a deliberate property, not a gap.

## Capabilities

### New Capabilities
(none — both affected capabilities already have spec coverage)

### Modified Capabilities
- `sdf-services-tasks`: button-gesture-to-admin-action mapping loses the Triple-click and Hold-3s bindings; the pre-enrollment bootstrap special-case is simplified accordingly.
- `ble-companion-service`: adds two new admin-fingerprint-gated triggers (`ENROLL_ADMIN`, `ZB_JOIN`) reachable only from an authenticated companion session, following the existing pending-admin-action request/authorize/respond pattern.

## Impact

**Code:**
- `firmware/components/sdf_services/src/sdf_services_button.c` — remove the Triple-click and Hold-3s registrations; simplify the pre-enrollment bootstrap branch.
- `firmware/components/sdf_services/src/sdf_services_admin.c` — extend pending-admin-action handling so `ENROLL_ADMIN`/`ZB_JOIN` pending requests originating from BLE carry a connection handle for result routing (parallel to how `WEB_REG_AUTH` and, once landed, `NUKI_REPAIR` already do this).
- `firmware/components/sdf_app/src/sdf_app.c` — `sdf_app_on_admin_action()`'s existing `ENROLL_ADMIN`/`ZB_JOIN` cases are unaffected in what they *do*; add result routing back to the originating BLE connection when the request came from BLE (vs. the button, which has none).
- `firmware/components/sdf_ble_companion/src/sdf_ble_companion.c` and its header — two new authenticated write ops for requesting `ENROLL_ADMIN` and `ZB_JOIN`, each requiring an existing logged-in session.
- `openspec/specs/sdf-services-tasks/spec.md`, `openspec/specs/ble-companion-service/spec.md` — delta specs for this change.
- `doc/First Time Flow Concept.md`, `doc/user_manual.md` — documentation currently describes Triple-click and Hold-3s for these actions; needs updating to describe the companion-app flow instead.
- `web-companion/app.js` (and related UI) — add authenticated admin UI affordances to request additional-admin enrollment and Zigbee join, mirroring whatever UI pattern `nuki-pairing-setup-flow` establishes for its Nuki re-pair trigger.

**Relationship to other in-progress work:** this change directly builds on the BLE-triggered admin-action pattern (`SDF_SERVICES_ADMIN_ACTION_NUKI_REPAIR`, pending-action-with-connection-routing) introduced by `nuki-pairing-setup-flow`. It should be implemented after (or alongside) that change, reusing its plumbing rather than duplicating it. It does not touch `NUKI_PAIR`/`NUKI_REPAIR`, `FACTORY_RESET`, or the separately-discussed BLE credential-hardening (salted/KDF'd password hashing, lockout) or link-layer accept-list work — those remain out of scope here.
