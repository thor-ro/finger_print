## Context

The physical button (`sdf_services_button.c`) currently maps five distinct gestures to five admin actions statically at registration time:

| Gesture | Action |
|---|---|
| Single-click | `ENROLL` |
| Triple-click | `ENROLL_ADMIN` |
| Double-click | `NUKI_PAIR` |
| Hold 3s | `ZB_JOIN` |
| Hold 8s | `FACTORY_RESET` |

Every non-enroll action, when the device is unclaimed (`enrolled_user_count == 0`), bypasses the admin-fingerprint pending-action gate entirely and fires immediately (`sdf_services_button.c:125-143`). This proposal removes Double-Press as a Nuki-pairing trigger and instead makes single-click's meaning state-dependent, so Nuki pairing is only ever reachable once an admin exists to gate it (pre-setup) or via an already-authenticated channel (post-setup: BLE Companion app, or a full factory reset which clears Nuki state and re-opens the setup sequence).

The reusable precedent for "BLE request → pending state → on-device admin fingerprint scan → routed result" is `SDF_SERVICES_ADMIN_ACTION_WEB_REG_AUTH` (`sdf_services_admin.c`, `sdf_app.c:764-833`), which this design follows for the new BLE-triggered Nuki re-pair action.

## Goals / Non-Goals

**Goals:**
- Make single-click's action state-dependent: `ENROLL` (unclaimed) → `NUKI_PAIR` (claimed, setup incomplete) → `ENROLL` (setup complete).
- Retire Double-Press as a trigger for anything; free the gesture.
- Close the pre-enrollment unauthenticated-execution gap **for Nuki pairing specifically** — once Double-Press no longer maps to `NUKI_PAIR`, and single-click's unclaimed-device meaning stays `ENROLL`, there is no button path left that reaches `NUKI_PAIR` without an admin already existing.
- After setup is complete, make Nuki re-pairing reachable only via: (a) factory reset, or (b) a new admin-fingerprint-gated BLE Companion action.
- Derive "setup complete" from existing persisted state (`sdf_storage_nuki_load()`), no new NVS flag.

**Non-Goals:**
- Closing the analogous pre-enrollment unauthenticated-execution gap for `ZB_JOIN` (Hold-3s) or `FACTORY_RESET` (Hold-8s). See Decisions below — explicitly deferred.
- Any change to the BLE Companion Service's credential hardening (salt/KDF, lockout) or the separately-discussed link-layer accept-list. Those are independent, not-yet-proposed changes.
- Changing what a factory reset erases, or how it's triggered (Hold-8s, unchanged).
- Adding a new persisted "setup complete" flag — deliberately avoided in favor of deriving it from `sdf_storage_nuki_load()`.

## Decisions

### 1. Single-click dispatch becomes a 3-way state check, not a new gesture

At press time (not registration time), `sdf_button_cb()`'s single-click handler evaluates:
1. `enrolled_user_count == 0` → `ENROLL` (unchanged from today).
2. `enrolled_user_count > 0` AND `sdf_storage_nuki_load(...) != ESP_OK` → `NUKI_PAIR`.
3. `enrolled_user_count > 0` AND `sdf_storage_nuki_load(...) == ESP_OK` → `ENROLL` (unchanged from today's claimed-device behavior).

Case 2 still passes through the existing pending-admin-action gate (an admin must already exist by definition in this branch, so the fingerprint-scan requirement in the "claimed" path of `sdf_button_cb()` applies unchanged — no new authorization code needed, just a new value fed into the existing `action` variable).

**Alternative considered**: keep Double-Press as the Nuki trigger but simply add a fingerprint gate to it pre-enrollment. Rejected — it doesn't free the gesture, it keeps two gestures serving what's really one sequential setup flow, and it still requires bespoke unclaimed-device gating logic that the state-dependent single-click approach gets for free (an admin necessarily exists in branch 2 above).

### 2. Double-Press is retired, not reassigned

The `BUTTON_DOUBLE_CLICK` → `SDF_SERVICES_ADMIN_ACTION_NUKI_PAIR` registration (`sdf_services_button.c:206-207`) is deleted outright. No new gesture is registered in its place. The gesture is left available for a future capability; assigning it to something new is out of scope here.

**Alternative considered**: repurpose Double-Press for the new BLE-triggered repair confirmation (e.g., "press twice to confirm Nuki repair request seen over BLE"). Rejected — it would reintroduce a physical-gesture dependency for a flow explicitly meant to be admin-fingerprint-gated instead, and duplicates the fingerprint-scan confirmation this design already relies on.

### 3. `ZB_JOIN` / `FACTORY_RESET` pre-enrollment gap: explicitly deferred, not fixed here

This proposal's own investigation surfaced that `ZB_JOIN` and `FACTORY_RESET` share the same unauthenticated pre-enrollment execution path as the old `NUKI_PAIR` (Double-Press) did. This design deliberately does **not** close that gap, for two reasons:
- **Scope discipline**: the proposal is titled and scoped as a Nuki-pairing/button-gesture refactor; `ZB_JOIN`/`FACTORY_RESET` are a related but separable concern with their own trade-offs (e.g., is unauthenticated `FACTORY_RESET` on an already-empty, unclaimed device actually a meaningful risk, since there's nothing yet to erase? Is unauthenticated `ZB_JOIN` pre-enrollment a real rogue-network risk worth its own analysis?). Bundling them here would blur the change's review surface.
- **No shared mechanism to piggyback on**: unlike Nuki pairing, `ZB_JOIN` and `FACTORY_RESET` aren't being restructured into a sequential single-click flow, so closing their gap would require inventing new gating logic in this same change rather than getting it as a side effect of the redesign.

This is tracked as explicit follow-up work, not silently dropped. See Risks/Trade-offs.

### 4. New BLE-triggered Nuki re-pair action mirrors `WEB_REG_AUTH`, as its own pending-action id

A new pending-admin-action id, `SDF_SERVICES_ADMIN_ACTION_NUKI_REPAIR`, is introduced (distinct from the existing `NUKI_PAIR`, which remains the pre-setup, button-triggered id). Flow:
1. An already-authenticated BLE Companion client writes a re-pair request (a new write op on the existing authenticated Companion Service transport, gated by the same connection-level auth as Config/Enroll/OTA today).
2. `sdf_ble_companion` fires an `on_nuki_repair_request` callback (parallel to the existing `on_auth_request` used for `WEB_REG_AUTH`), tracking the originating connection handle.
3. `sdf_app` sets `pending_admin_action = NUKI_REPAIR` via the existing admin-task plumbing; LED pulses a distinct color.
4. Admin scans their fingerprint on the physical device within the existing pending-action timeout.
5. On approval, `sdf_app_on_admin_action()`'s `NUKI_REPAIR` case invokes the same underlying `sdf_nuki_ble_set_enabled(&s_ble, true)` / `sdf_nuki_ble_start(&s_ble)` calls the button-triggered `NUKI_PAIR` case already uses.
6. The result (approved/denied/timeout) is routed back to the originating BLE connection, following the same "always resolves" guarantee `WEB_REG_AUTH` already provides (`sdf-services-tasks` spec, "Pending registration always resolves" scenario) so no client is left waiting indefinitely.

**Why a separate action id instead of reusing `NUKI_PAIR`**: the two are reached via different triggers (button vs. BLE), occur in different device states (pre-setup vs. post-setup), and only the BLE-triggered one needs a connection-handle-routed result. Reusing the same id would conflate "which flow is this actually satisfying" in the pending-action state machine and LED semantics. The underlying pairing operation they invoke is intentionally the same.

**Why gated only by admin fingerprint, not BLE credentials alone**: consistent with the proposal's explicit requirement that "a BLE request alone, without an on-device fingerprint scan, is never sufficient" — this keeps re-pairing as strong as the original setup-time pairing, which also required a fingerprint scan.

### 5. "Setup complete" derivation stays a runtime check, not a cached flag

`sdf_storage_nuki_load()` is already called at boot (`sdf_app.c:1531-1542`) to restore credentials into RAM; the button callback and the new BLE trigger both call it (or check the already-loaded in-RAM credential state, whichever is cheaper at the call site) rather than introducing a new persisted boolean. This avoids a second source of truth that could drift from the actual credential state (e.g., if Nuki credentials are cleared by a future code path without remembering to also clear a separate flag).

## Risks / Trade-offs

- **[Risk] Deferring the `ZB_JOIN`/`FACTORY_RESET` pre-enrollment gap leaves a known unauthenticated-execution path in place.** → Mitigation: explicitly documented here and in the proposal (not silently dropped); recommend a dedicated follow-up change once this one lands, scoped to auditing all pre-enrollment admin-action execution, not just button-triggered ones.
- **[Risk] Retiring Double-Press without reassigning it reduces the button's total addressable action count from five to four**, which could constrain future gesture needs. → Mitigation: acceptable trade-off; the gesture remains available to assign later, and freeing it was an explicit goal (an already-crowded single-button interface).
- **[Risk] The state-dependent single-click check adds a flash read (`sdf_storage_nuki_load()` or equivalent) on every single-click, including the hot-path "enroll a new user" case post-setup.** → Mitigation: this is the same NVS read already performed at boot and is not expected to be latency-sensitive for a human-timescale button press; if profiling later shows it matters, the in-RAM credential state already tracked by `sdf_app` can be consulted instead of re-reading storage.
- **[Trade-off] A device that completes admin enrollment but is interrupted before completing Nuki pairing is stuck needing a second single-click** (rather than pairing being automatic/immediate after enrollment). → Accepted: matches the explicit ask ("start with enrollment and then nuki pairing by single button click" — i.e., two distinct clicks, not one action chaining into the next automatically) and preserves the admin-fingerprint gate as a deliberate, visible step rather than an implicit one.

## Migration Plan

- No data migration required — no schema or NVS format changes.
- Devices already past setup (Nuki credentials already persisted) transition immediately and silently to the new single-click semantics (`ENROLL`) on firmware update; this matches their existing single-click behavior today, so no user-visible change for already-set-up devices.
- Devices mid-setup at the moment of a firmware update (admin enrolled, Nuki not yet paired) will find single-click now triggers `NUKI_PAIR` instead of requiring Double-Press — this is the intended new behavior, and is a strict usability improvement (no re-learning needed beyond "press the same button again").
- Rollback: reverting the firmware update restores Double-Press as the Nuki trigger; no persisted state introduced by this change needs to be undone.

## Open Questions

- Should the `ZB_JOIN`/`FACTORY_RESET` pre-enrollment gap (deferred per Decision 3) be tracked as a formal follow-up OpenSpec change now, or left informal until prioritized? (Recommendation: file a follow-up change proposal once this one is implemented, rather than leaving it purely as a code comment.)
- Exact BLE wire format for the new Nuki re-pair request/response (which characteristic, op code, and payload shape) is left to implementation (tasks.md / code review), following the existing Companion Service write-op conventions rather than being pinned down here.
