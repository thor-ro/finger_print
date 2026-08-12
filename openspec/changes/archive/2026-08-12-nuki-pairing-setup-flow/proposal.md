## Why

Nuki pairing is a one-time (or rare) part of initial device setup, but it currently occupies its own permanent gesture — Double-Press — on an already-crowded single-button interface (Single/Double/Triple-click plus two hold-durations already map to five distinct admin actions). Worse, that gesture is reachable with **zero authentication** before any admin fingerprint has been enrolled: `sdf_button_cb()`'s unclaimed-device bootstrap path (`sdf_services_button.c:125-143`) only special-cases `ENROLL`/`ENROLL_ADMIN`; every other action — including `NUKI_PAIR` via Double-Press, `ZB_JOIN`, and `FACTORY_RESET` — is executed immediately via `admin_action_cb()` with no fingerprint check at all when `enrolled_user_count == 0`. On a freshly unboxed or freshly factory-reset device, anyone who reaches the button before the legitimate owner finishes admin enrollment can start Nuki pairing unauthenticated. Folding Nuki pairing into the sequential single-click setup flow (enroll admin, then pair Nuki) closes this gap for good, because "pair Nuki" only becomes single-click's meaning *after* an admin already exists to authorize it — and it frees the Double-Press gesture entirely.

## What Changes

- Single-click semantics become state-dependent across three phases instead of two:
  - Unclaimed (0 enrolled users) → admin enrollment (unchanged).
  - Claimed, but no Nuki credentials persisted yet (setup not yet complete) → Nuki pairing, gated behind admin-fingerprint authorization (closing the pre-enrollment gap described above).
  - Claimed, and Nuki already paired (setup complete) → standard user enrollment (today's existing "claimed" single-click behavior, unchanged).
- "Setup complete" is derived from existing persisted state — `sdf_storage_nuki_load()` returning `ESP_OK` — not a new NVS flag.
- The `BUTTON_DOUBLE_CLICK` → `SDF_SERVICES_ADMIN_ACTION_NUKI_PAIR` registration is removed. Double-Press is retired as a Nuki-pairing trigger and left unmapped (available for future use, not assigned here).
- **BREAKING**: once setup is complete, Nuki pairing is no longer reachable from any button gesture. Re-pairing is only reachable via:
  - A full factory reset (already clears Nuki credentials today, which naturally re-opens the single-click-pairs-Nuki window on the next admin enrollment cycle), or
  - A new admin-fingerprint-gated trigger exposed over the BLE Companion Service, following the same pending-admin-action pattern already used by `SDF_SERVICES_ADMIN_ACTION_WEB_REG_AUTH` (request arrives over BLE → device enters a pending state → admin scans their fingerprint on the physical device → pairing starts). A BLE request alone, without an on-device fingerprint scan, is never sufficient.

## Capabilities

### New Capabilities
(none — both affected capabilities already have spec coverage)

### Modified Capabilities
- `sdf-services-tasks`: button-gesture-to-admin-action mapping changes (state-dependent single-click, Double-Press retirement) and the pre-enrollment admin-action authorization gap is closed for all actions, not just Nuki pairing.
- `ble-companion-service`: adds a new admin-fingerprint-gated Nuki re-pairing trigger, reachable only after setup is complete and only via the existing on-device fingerprint authorization pattern (not BLE credentials alone).

## Impact

**Code:**
- `firmware/components/sdf_services/src/sdf_services_button.c` — remove the Double-Click registration; make single-click dispatch state-dependent (query Nuki-paired state before choosing `ENROLL` vs `NUKI_PAIR`).
- `firmware/components/sdf_services/src/sdf_services_admin.c`, `sdf_services.c` — pending-admin-action handling for the state-dependent dispatch; the pre-enrollment "unclaimed device executes non-enroll actions unauthenticated" branch needs to be closed off (or explicitly scoped: this proposal closes it for Nuki pairing by removing the button path to it; whether `ZB_JOIN`/`FACTORY_RESET` pre-enrollment execution is addressed here or left as a follow-up is a design decision, see design.md).
- `firmware/components/sdf_app/src/sdf_app.c` — extend the `WEB_REG_AUTH`-style admin-action pending pattern to cover a new BLE-triggered Nuki-repair request.
- `firmware/components/sdf_ble_companion/src/sdf_ble_companion.c` and its header — new BLE-exposed trigger for authenticated clients to request Nuki re-pairing (subject to normal Companion Service authentication plus the on-device fingerprint scan).
- `firmware/components/sdf_storage` — no schema change; reuse `sdf_storage_nuki_load()`/`sdf_storage_nuki_clear()` as the setup-complete signal.
- `openspec/specs/sdf-services-tasks/spec.md`, `openspec/specs/ble-companion-service/spec.md` — delta specs for this change.
- `doc/First Time Flow Concept.md`, `doc/user_manual.md` — documentation currently describes Double-Press as the Nuki-pairing gesture and will need updating to reflect the new sequential single-click flow.

**Relationship to other in-progress security work:** this change is scoped to the Nuki-pairing/button-gesture refactor only. It does not include the separately-discussed BLE Companion credential hardening (salted/KDF'd password hashing, per-username lockout) or the BLE link-layer accept-list work — those remain out of scope here, though the new BLE-triggered Nuki re-pair action will eventually sit behind whatever authentication that work establishes for the Companion Service.
