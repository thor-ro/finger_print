## Context

See `proposal.md` - Why. Two BLE-triggered flows currently reach `sdf_services` differently:

- NUKI_REPAIR / ENROLL_ADMIN / ZB_JOIN: `sdf_ble_companion`'s GATT access callback invokes `sdf_app_on_ble_admin_action_request()` directly (NimBLE host task), which calls `sdf_services_request_admin_action()` synchronously. No event router involved.
- WEB_REG_AUTH: the same GATT callback family invokes `sdf_ble_companion_on_auth_request()`, which builds a 64-byte-payload event and emits it through the router. The router queues it, its own task dequeues it, and only then does `sdf_app_on_web_reg_auth_request()` call the same two `sdf_services` functions the other flows call directly.

`sdf_services` already owns single-slot state for this request (`request_web_username`, `request_web_password_hash`, gated by `web_reg_auth_pending`), with existing accessors (`sdf_services_set_web_reg_auth`, `_get_web_reg_auth`, `_get_web_reg_password_hash`, `_clear_web_reg_auth`). This design routes WEB_REG_AUTH through the same direct-call shape as its siblings and stops the RESULT event from round-tripping data through that owned state.

## Goals / Non-Goals

**Goals:**
- Credential material (username, password hash) never enters an `sdf_event_router_event_t` payload or any FreeRTOS queue.
- `sdf_event_router_event_t` shrinks to the size dictated by its next-largest member (`audit_payload_t`, 16 bytes) once the two web-reg payloads are gone/trimmed.
- No change to externally observable behavior: same GATT semantics, same admin-approval flow, same persisted user record, same BLE reply contract.

**Non-Goals:**
- Not reworking `audit_payload_t` or any other union member to shrink further (out of scope; the 16-byte union max is already a ~3.6x win over today's 64 bytes and matches the target of this change).
- Not changing error-reporting behavior to the BLE client for the (today: silent, fire-and-forget) failure paths of `sdf_services_set_web_reg_auth()` / `sdf_services_request_admin_action()`.
- Not changing `sdf_services`' locking model or `SDF_SERVICES_LOCK_WAIT_MS` bound.

## Decisions

**1. Eliminate `SDF_EVENT_ROUTER_WEB_REG_AUTH_REQUEST` entirely, rather than shrinking it to a zero-payload notification.**
`sdf_ble_companion_on_auth_request()` calls `sdf_services_set_web_reg_auth()` then `sdf_services_request_admin_action(SDF_SERVICES_ADMIN_ACTION_WEB_REG_AUTH)` directly, in the NimBLE host task — mirroring `sdf_app_on_ble_admin_action_request()`'s existing handling of NUKI_REPAIR/ENROLL_ADMIN/ZB_JOIN in the same file. Alternative considered: keep a zero-payload event purely to preserve the router hop for architectural symmetry with the RESULT side. Rejected — it would keep an extra queue hop and dispatch-task latency for no behavioral benefit, and it leaves the odd-one-out asymmetry with the three sibling admin actions that already skip the router.

**2. `SDF_EVENT_ROUTER_WEB_REG_AUTH_RESULT` payload shrinks to `{ bool authorized; }` (drops `username` and `permission`).**
Both dropped fields are pure round-trips of `sdf_services`' owned state today (`sdf_services.c` reads `request_web_username`/`request_web_permission` under lock to build the event; the sole consumer reads them straight back out). `sdf_app_on_web_reg_auth_result()` instead calls the existing `sdf_services_get_web_reg_auth()` accessor to obtain both. `authorized` is kept, even though it is a compile-time-constant `true` at today's only emission site, as an explicit, self-documenting invariant check at the consumer rather than an implicit "this event type only fires on approval" assumption — cheap (1 byte) insurance against a future code path emitting this event on a non-approval outcome.

**3. Accept a bounded blocking mutex-take inside the NimBLE GATT access callback.**
`sdf_services_set_web_reg_auth()` and `sdf_services_request_admin_action()` each take `s_state.lock` with the existing `SDF_SERVICES_LOCK_WAIT_MS` (250ms) bound. Calling them from the BLE host task is not a new pattern — `sdf_app_on_ble_admin_action_request()` already does exactly this, in the same callback family, for the three sibling actions. No new timing risk is introduced.

**4. No new atomicity between the two `sdf_services` calls.**
`sdf_services_set_web_reg_auth()` and `sdf_services_request_admin_action()` remain two separate locked calls, called sequentially without a combined lock — identical to how `sdf_app_on_web_reg_auth_request()` calls them today. Moving the call site one hop earlier (BLE task instead of router task) does not change this; no new race is introduced.

**5. Preserve today's silent-drop failure semantics.**
If either call returns a non-OK `esp_err_t` (e.g., a request is already pending), the BLE callback logs and drops, matching the current behavior when `sdf_event_router_emit()` fails (e.g., a full queue). Surfacing an explicit rejection to the GATT client is a reasonable follow-up but is out of scope here to keep this change behavior-preserving.

## Risks / Trade-offs

- [Blocking mutex-take moves into the NimBLE host task's GATT callback] → Mitigated by precedent: the sibling admin-action callback already does this in the same file/task; the 250ms bound is unchanged and contention is rare under the single-in-flight-request model `web_reg_auth_pending` enforces.
- [Removing an event type is a breaking change to the router's internal event set] → Confirmed via repo-wide grep that `sdf_app.c` is the only producer and only consumer of `SDF_EVENT_ROUTER_WEB_REG_AUTH_REQUEST`; safe to remove. Re-grep before archiving in case other in-flight changes touched it.
- [`sizeof(sdf_event_router_event_t)` changes] → No code serializes this struct over a wire or persists it; only in-RAM FreeRTOS queues reference it, so a size change is safe. `test_sdf_event_router.c` does not assert on `sizeof()`.
- [RESULT handler now depends on `web_reg_auth_pending` still being true when it reads via the accessor] → Verified: `sdf_app_on_web_reg_auth_result()` reads before calling `sdf_services_clear_web_reg_auth()` at the end of the same function, and the mutually-exclusive denial/timeout path (`sdf_app_on_admin_action_complete`) never runs concurrently for the same pending request (per the existing comment in `sdf_services.c` documenting that ADMIN_ACTION_COMPLETE and WEB_REG_AUTH_RESULT are never both emitted for one request).

## Migration Plan

Single-shot internal refactor; no persisted data format or wire protocol involved (the event struct is transient, in-RAM only). No phased rollout or feature flag needed — ship as one change. Rollback is a plain revert if needed.

## Open Questions

- Whether to also surface a synchronous rejection to the BLE client when `sdf_services_set_web_reg_auth()` / `sdf_services_request_admin_action()` fail (today and after this change: silent drop) — deferred as a possible follow-up change, not required for this one.
