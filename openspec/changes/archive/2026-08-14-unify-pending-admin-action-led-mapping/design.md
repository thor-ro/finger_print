## Context

`pending_admin_action` is a single field on `sdf_services_state_t`. Three functions can set it, and each one independently decides which LED to pulse:

```
                     ┌──────────────────────────────┐
                     │  s->pending_admin_action     │  ← one field
                     └──────────────▲───────────────┘
                                    │
        ┌───────────────────────────┼───────────────────────────┐
        │                           │                           │
sdf_button_dispatch_action   sdf_admin_task            sdf_services_
(sdf_services_button.c)      ADMIN_ACTION_REQUEST      request_admin_action
        │                    (sdf_services_admin.c)    (sdf_services.c)
        │                           │                           │
   switch (7 cases)           switch (7 cases)            switch (2 cases)
   blue/yellow/purple/        blue/yellow/purple/         white/cyan
   red/cyan                   red/white/cyan
        │                           │                           │
        └──── ✗ no BLE_PAIRING ─────┴──── ✗ no BLE_PAIRING ─────┘
              _WINDOW case                  _WINDOW case
```

Three copies of one decision, kept in sync by hand. They are already out of sync.

## Goals / Non-Goals

**Goals**
- One function owns the action → LED-indication mapping; the three call sites consult it.
- The mapping covers the full `sdf_services_admin_action_t` set, so adding a new action forces an explicit decision rather than silently inheriting "no feedback."
- No color reassignments — the resulting table is exactly the union of today's three tables.

**Non-Goals**
- Changing which LED colors mean what.
- Changing LED behavior on authorization success, denial, or pending-action timeout.
- Changing who is allowed to set a pending action, or from where. `centralize-unclaimed-device-bootstrap` and `dispatch-admin-actions-off-esp-timer` handle that; this change is deliberately mechanical.

## Decisions

### Decision 1: A helper function, not a static table
Use a small function taking `sdf_services_admin_action_t` and performing the `switch`, rather than a `const` array indexed by the enum. The action enum is sparse and explicitly numbered (`BLE_PAIRING_WINDOW = 9`), so an indexed table invites a silent out-of-bounds read if the enum gains a gap. A `switch` also lets the compiler warn on unhandled enumerators when the default case is omitted, which is the safety property this change is buying.

### Decision 2: Where the helper lives
Place it alongside the other shared services internals so all three translation units can call it without a new dependency edge. `sdf_services_button.c`, `sdf_services_admin.c`, and `sdf_services.c` are all part of the `sdf_services` component and already share `sdf_services_internal.h`; the declaration goes there and the definition in whichever of those TUs already owns shared state helpers.

The helper depends only on `led_*` (already a dependency of all three call sites) and takes no lock — it is a pure action → `led_pulse_*` dispatch. Callers keep whatever locking they already do; two of the three call it while holding `s->lock`, which is safe because `led_post_cmd()` is non-blocking (`led.c:208-216`).

### Decision 3: `BLE_PAIRING_WINDOW` keeps cyan
It already pulses cyan in the button path, deliberately matching `NUKI_REPAIR`'s cyan ("both are BLE-Companion-Service-related admin actions", `sdf_services_button.c:186-188`). Two actions sharing a color is pre-existing and intentional; this change preserves it rather than re-opening the palette.

### Decision 4: Handle the full enum explicitly
Enumerate every `sdf_services_admin_action_t` value in the switch. Actions with no meaningful indication (e.g. `NONE`) get an explicit no-op case rather than falling into `default`. Keep a `default` for defensive robustness against a cast-in integer, but the intent is that no real enumerator relies on it.

## Risks / Trade-offs

- **[Risk]** A caller that previously produced *no* pulse for some action now produces one, which could surprise an existing test asserting on LED state. → **Mitigation:** the host tests assert on `pending_admin_action`, not on LED output; run the suite and update any LED assertion that turns up. The behavior change is the point of the change, so a failing assertion is a signal to update it, not to revert.
- **[Trade-off]** The three call sites remain three call sites — this unifies the *mapping*, not the *dispatch*. Unifying dispatch is `dispatch-admin-actions-off-esp-timer`'s job. Doing only the mapping here keeps this change mechanical and independently reviewable, at the cost of leaving the structural duplication in place a little longer.

## Migration Plan

Single mechanical refactor, no staged rollout, no persisted-state or protocol implications. Land it before `dispatch-admin-actions-off-esp-timer` so that change inherits a complete mapping.

## Open Questions

None.
