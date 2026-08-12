## Context

After `nuki-pairing-setup-flow` lands, the button interface will be down to two gestures: single-click (state-dependent: `ENROLL` / `NUKI_PAIR` / `ENROLL`) and Hold-8s (`FACTORY_RESET`). This change removes the remaining two administrative gestures — Triple-click (`ENROLL_ADMIN`) and Hold-3s (`ZB_JOIN`) — and relocates their *origin* to the BLE Companion Service, while keeping their existing on-device Admin-fingerprint authorization requirement intact.

The reusable precedent is the pending-admin-action pattern: a request enters a pending state, an Admin fingerprint scan on the physical device authorizes or denies it, and (for BLE-originated requests) the result is routed back to the requesting connection. `SDF_SERVICES_ADMIN_ACTION_WEB_REG_AUTH` established this; `nuki-pairing-setup-flow`'s new `NUKI_REPAIR` action id applies it a second time. This design applies it a third and fourth time, for `ENROLL_ADMIN` and `ZB_JOIN`.

## Goals / Non-Goals

**Goals:**
- Remove the Triple-click and Hold-3s button registrations entirely; do not reassign them.
- Let an authenticated companion-app admin request `ENROLL_ADMIN` or `ZB_JOIN` over BLE, gated by the same on-device Admin fingerprint scan the button path already required.
- Route the pending request's outcome (approved/denied/timeout) back to the originating BLE connection, since — unlike a button press — a BLE client is waiting for a definite answer.
- Close the `ZB_JOIN` pre-enrollment unauthenticated-execution path as a side effect of removing its button binding.
- Simplify the pre-enrollment bootstrap branch in `sdf_button_cb()` now that `ENROLL_ADMIN` is unreachable from the button.

**Non-Goals:**
- Changing what `ENROLL_ADMIN` or `ZB_JOIN` *do* once authorized — `sdf_app_on_admin_action()`'s existing case bodies are reused unchanged; only how the request is originated and how its result is communicated changes.
- Touching `FACTORY_RESET` or Hold-8s. Factory reset intentionally stays physical-button-only: it's a destructive/recovery action, and requiring a human physically present at the device (not just holding an authenticated phone session) is a deliberate property worth preserving, not a gap to close.
- Touching `NUKI_PAIR`/`NUKI_REPAIR` or single-click — fully owned by `nuki-pairing-setup-flow`.
- Re-litigating BLE Companion Service authentication itself (session login, credential hardening) — this change assumes a client is already logged in before it can request either action, using whatever authentication mechanism the Companion Service already has.

## Decisions

### 1. Reuse the existing `ENROLL_ADMIN`/`ZB_JOIN` action ids rather than minting new ones

`nuki-pairing-setup-flow` introduced a *distinct* `NUKI_REPAIR` id instead of reusing `NUKI_PAIR`, because `NUKI_PAIR` remained separately reachable via button (in the pre-setup-complete state) after that change — two different trigger routes needed to stay distinguishable in the pending-action state machine and LED semantics.

That doesn't apply here: this change removes the button path to `ENROLL_ADMIN` and `ZB_JOIN` *entirely*, so after this change lands each id has exactly one possible origin (BLE). Reusing the existing ids means `sdf_app_on_admin_action()`'s case bodies, the LED pulse colors, and the core `pending_admin_action` state machine all keep working unmodified — only the request's origin (button ISR vs. BLE write) and the presence of a connection handle to notify on resolution are new.

**Alternative considered**: mint `ENROLL_ADMIN_BLE`/`ZB_JOIN_BLE` variants for symmetry with `NUKI_REPAIR`. Rejected — adds a distinction with no behavioral difference to preserve, since there's no second trigger route left to disambiguate from.

### 2. Connection-tracking added at the pending-action layer, not duplicated per action

`sdf_services_admin.c`'s pending-action state gains an optional "originating BLE connection handle" field (unset/invalid for button-originated requests, set for BLE-originated ones). On resolution, if a handle is present, the result is routed back to that connection using the same GATT notification mechanism `WEB_REG_AUTH` already uses; if absent (button-originated), behavior is unchanged (LED feedback only, no reply to route). This keeps the "always resolves, never leaves a client waiting indefinitely" guarantee general across all pending-action types rather than reimplementing it per action.

**Alternative considered**: give each BLE-originated action type (`WEB_REG_AUTH`, `NUKI_REPAIR`, `ENROLL_ADMIN`, `ZB_JOIN`) its own separate response-routing code path. Rejected — this is exactly the kind of duplication a shared "optional originating connection" field on the pending-action state avoids; four near-identical copies of "notify this connection of the outcome" is a maintenance liability the first three changes (this one included) should collapse into one mechanism.

### 3. `ZB_JOIN`'s pre-enrollment gap closes structurally, not by adding a check

Rather than adding a fingerprint gate to the pre-enrollment bootstrap branch for `ZB_JOIN` (which `nuki-pairing-setup-flow`'s design explicitly deferred as out of scope for that change), this change removes the only path that reached `ZB_JOIN` unauthenticated in the first place — its button binding. Pre-enrollment, `ZB_JOIN` simply becomes unreachable from any source (there's no companion-app session possible either, since companion login itself requires an already-enrolled user record). This is a natural consequence of the button-to-companion-app move, not a separate fix.

Note this does **not** close the equivalent gap for `FACTORY_RESET` (Hold-8s), which is intentionally out of scope (see Non-Goals) — a device with zero enrolled users and physical access to the button can still trigger an unauthenticated factory reset. That gap remains open and untouched by this change, exactly as `nuki-pairing-setup-flow`'s design.md flagged it as deferred follow-up work.

### 4. `ENROLL_ADMIN` request UX mirrors `ENROLL`'s existing two-scan shape

Authorizing `ENROLL_ADMIN` (Admin scans to authorize) is a separate physical scan from the subsequent fingerprint-enrollment scan(s) that capture the *new* admin's fingerprint template — this two-scan shape already exists for both `ENROLL` and `ENROLL_ADMIN` today via the button and is unchanged by this design. The companion app's role is only to originate the authorization request and display its outcome; it does not participate in capturing the new admin's fingerprint, which remains an on-device-only operation.

## Risks / Trade-offs

- **[Risk] An admin without their phone can no longer promote a new admin or open a Zigbee join window from the device alone.** → Mitigation: accepted trade-off, consistent with the proposal's intent (these are rare actions where a richer companion-app UI outweighs the convenience of a bare physical gesture); Hold-8s `FACTORY_RESET` remains as a phone-independent recovery path if the companion app is ever unreachable.
- **[Risk] Adding a shared "originating connection" field to the pending-action state increases its complexity slightly.** → Mitigation: it's additive (optional field, `NULL`/invalid for button-originated actions) and directly reduces duplication versus four separate response-routing implementations; net complexity is lower, not higher.
- **[Trade-off] `FACTORY_RESET`'s pre-enrollment unauthenticated-execution gap remains open after this change**, even though `ZB_JOIN`'s closes as a side effect. → Accepted and explicitly documented (again) here so it isn't lost between changes; recommend the same follow-up change suggested in `nuki-pairing-setup-flow` also cover `FACTORY_RESET` specifically, weighing whether physical-presence-only is the right final answer for it (see Non-Goals).

## Migration Plan

- No data migration required — no schema or NVS format changes; reuses existing action ids and existing pending-action machinery.
- Devices update to this firmware and immediately lose the Triple-click/Hold-3s gestures; any admin relying on them must use the companion app afterward. This should be called out in release notes given it's a **BREAKING** behavior change (see proposal).
- Rollback: reverting the firmware update restores the button bindings; no persisted state introduced by this change needs to be undone.

## Open Questions

- Should the companion-app UI for `ENROLL_ADMIN`/`ZB_JOIN` requests be built now (task-listed here) or treated as a thin follow-up once the shared BLE request/response UI pattern from `nuki-pairing-setup-flow`'s Nuki re-pair trigger exists and can be copied? (Recommendation: sequence this change after `nuki-pairing-setup-flow` lands and copy its UI pattern, per the proposal's stated dependency.)
- Should `FACTORY_RESET`'s pre-enrollment gap be folded into this change's scope after all, since it's now the *only* remaining pre-enrollment unauthenticated-execution path? Left open here; the Non-Goals section above records the reasoning for keeping it out, but this is worth revisiting once both this change and a `ZB_JOIN`-gap retrospective are further along.
