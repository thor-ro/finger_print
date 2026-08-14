## Context

Two functions can set `pending_admin_action`, but only one of them contains the unauthenticated-execution bypass:

```
                    admin action requested
                              │
          ┌───────────────────┴───────────────────┐
          │                                       │
   physical button                        remote (BLE Companion)
          │                                       │
sdf_button_dispatch_action()          sdf_services_request_admin_action()
   (sdf_services_button.c)                   (sdf_services.c)
          │                                       │
    ┌─────┴─────┐                                 │
 users==0    users>0                              │
    │           │                                 │
 ┌──┴──┐        └──────────┬──────────────────────┘
 │     │                   │
ENROLL  else          set pending_admin_action
 │        │           await admin fingerprint
 │   action_cb()             │
 │   ✗ NO AUTH               │
 │        │            (on an unclaimed device this
request_enrollment     can never be satisfied — it
 ✗ NO AUTH             just times out)
```

The left subtree — the entire unauthenticated path — is implemented inside a function whose declared purpose is "shared dispatch body for a resolved admin action" from a button press. The right subtree has no equivalent branch, and nothing in the code says whether that is a decision or an oversight.

## Goals / Non-Goals

**Goals**
- One function answers "may this request execute without admin authorization?", consulted by every path that can set or execute an admin action.
- Request origin becomes an explicit input to that function, so the local-only property of the bypass is enforced by a parameter rather than by which file the code happens to live in.
- The bypass's real breadth is written down accurately in the spec.
- Behavior on every existing path is bit-for-bit unchanged.

**Non-Goals**
- Changing which actions are eligible, or narrowing the `else` branch. The `else` branch executing `BLE_PAIRING_WINDOW` and `FACTORY_RESET` unauthenticated on an unclaimed device is pre-existing, defensible, and out of scope — if it should be narrowed, that is its own change with its own risk discussion.
- Adding a bootstrap bypass to the remote path.
- Touching the admin-fingerprint gate, the pending-action timeout, or `sdf_services_execute_admin_action()`.

## Decisions

### Decision 1: An origin-parameterized authorization helper, not a flag on state
Model the decision as a function of `(action, origin)` returning whether to bypass, rather than storing an "is bootstrapping" flag on `sdf_services_state_t`. A stored flag would be readable — and therefore trustable — by code that never established it, which is precisely the failure mode this change exists to prevent. Passing origin at the call site forces each caller to assert what it is.

`origin` needs only two values today (local physical, remote request). Introduce it as an enum rather than a `bool`, so a future third origin (e.g. a wired console) has to state its intent rather than pick a side of a boolean.

### Decision 2: The helper owns the whole bypass body, not just the predicate
The bypass is not only a yes/no decision — it also clears `pending_admin_action` / `pending_admin_action_start_us` and then routes to one of two execution paths (`sdf_services_request_enrollment()` for `ENROLL`, the configured `admin_action_cb` for everything else). Splitting the predicate from the body would leave the routing duplicated at each call site, which is the same defect one layer down.

So the helper takes `(action, origin)` and either performs the bypassed execution and reports that it did, or reports that the caller should fall through to the ordinary pending-action flow.

### Decision 3: Lock discipline is preserved exactly
Today's code takes `s->lock`, checks the user count, and — in the bypass branch — **releases the lock before** calling `led_pulse_blue()` / `sdf_services_request_enrollment()` / `action_cb()`. That release is essential: `sdf_services_request_enrollment()` and the `admin_action_cb` both re-enter services code that takes the same lock. The helper must reproduce this precisely: acquire, decide, clear pending state, release, *then* execute.

This is the single highest-risk detail in the change. A refactor that accidentally holds the lock across `action_cb()` deadlocks the device on first press of an unclaimed unit.

### Decision 4: `sdf_services_request_admin_action()` routes through the helper too
Even though it passes remote origin and therefore always gets "no bypass," route it through the helper anyway. The value of a single decision point is lost if one of the two callers doesn't consult it — and it converts today's silent omission into a visible, reviewable `SDF_SERVICES_ADMIN_ORIGIN_REMOTE` argument.

### Decision 5: Where it lives
`sdf_services.c`, alongside `sdf_services_request_admin_action()` and `sdf_services_execute_admin_action()` — the code that already owns `pending_admin_action` and the admin-fingerprint gate. Declared in `sdf_services_internal.h` so `sdf_services_button.c` (and later `sdf_services_admin.c`) can call it.

## Risks / Trade-offs

- **[Risk — highest]** Getting the lock release wrong deadlocks an unclaimed device on the first button press, which is also the first thing anyone does with new hardware. → **Mitigation:** the acquire/decide/release/execute sequence is called out explicitly in tasks; add a host test that drives the bypass with the lock instrumented, and verify on real hardware with a factory-reset unit before merging.
- **[Risk]** Making the bypass a named, exported-within-the-component helper makes it *easier* to call from a new site — the opposite of hiding it. → **Mitigation:** the origin parameter is the guard. A new caller cannot obtain the bypass without explicitly claiming local physical origin, which is a reviewable one-token assertion at the call site.
- **[Trade-off]** Correcting the spec's description of the bypass breadth makes the unauthenticated `FACTORY_RESET` / `BLE_PAIRING_WINDOW` paths conspicuous in the spec where they were previously understated. That may prompt a decision to narrow them. That is a feature of writing it down, but it is deliberately not decided here.

## Migration Plan

Pure refactor, no persisted state, no protocol surface, no staged rollout. Land before `dispatch-admin-actions-off-esp-timer`.

## Open Questions

- Should the `else` branch (non-`ENROLL` actions executing unauthenticated on an unclaimed device) be narrowed to an explicit allowlist? Out of scope here, but now that it is accurately specified it is a reasonable follow-up to consider.
