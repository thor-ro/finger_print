## Why

The mapping from "an admin action just became pending" to "which LED pulse the user sees" is duplicated across **three** call sites, and the three copies have already diverged:

| Action | `sdf_button_dispatch_action()` (button) | `sdf_admin_task` (`ADMIN_ACTION_REQUEST`) | `sdf_services_request_admin_action()` (direct/BLE) |
|---|---|---|---|
| `ENROLL` / `ENROLL_ADMIN` | blue | blue | — |
| `NUKI_PAIR` | yellow | yellow | — |
| `ZB_JOIN` | purple | purple | — |
| `FACTORY_RESET` | red | red | — |
| `WEB_REG_AUTH` | — | white | white |
| `NUKI_REPAIR` | — | cyan | cyan |
| `BLE_PAIRING_WINDOW` | cyan | **missing** | **missing** |

Every cell in that table is reachable state — `pending_admin_action` is a single shared field, and which of the three sites sets it depends only on where the request originated, not on which action it is. So a hole in one column is a real user-visible defect waiting for the day that action starts arriving through that path: the device silently sits in a pending-admin-fingerprint state with no LED feedback, and the user has no signal that the device is waiting for them.

`BLE_PAIRING_WINDOW` is exactly that case today. It is currently only reachable via the button double-click, so the two holes are latent rather than live — but the moment any change routes admin actions through a different site (which `dispatch-admin-actions-off-esp-timer` does), the double-click loses its LED feedback with no compile-time warning.

The triplication is already known and documented in the code as a deliberate copy (`sdf_services.c:1099-1104`: *"mirrored here rather than shared, consistent with `sdf_button_cb`'s own separate copy"*). This change replaces the mirroring with a single source of truth.

## What Changes

- Introduce one shared function that maps a `sdf_services_admin_action_t` to its pending-action LED indication, covering the full action enum with a single explicit default.
- Replace all three inline `switch` statements — in `sdf_button_dispatch_action()` (`sdf_services_button.c`), in `sdf_admin_task`'s `SDF_EVENT_ROUTER_ADMIN_ACTION_REQUEST` handler (`sdf_services_admin.c`), and in `sdf_services_request_admin_action()` (`sdf_services.c`) — with a call to it.
- **Behavior change**: `BLE_PAIRING_WINDOW` now pulses cyan regardless of which path set it pending, and `WEB_REG_AUTH`/`NUKI_REPAIR`/`ENROLL`/`NUKI_PAIR`/`ZB_JOIN`/`FACTORY_RESET` likewise become path-independent. No existing color assignment changes; the union of the three tables becomes the single table.
- Remove the now-obsolete code comment in `sdf_services.c` explaining why the switch was mirrored.

## Capabilities

### Modified Capabilities
- `sdf-services-tasks`: adds a requirement that the pending-admin-action LED indication is a single, path-independent mapping over the full admin-action set. No existing requirement currently covers LED feedback for pending admin actions, so this is purely additive to that capability.

## Impact

- Code: `firmware/components/sdf_services/src/sdf_services_button.c`, `sdf_services_admin.c`, `sdf_services.c`, plus a declaration in `sdf_services_internal.h`.
- No public API change. `sdf_services_request_admin_action()`, `sdf_button_dispatch_action()`, and the event-router contract are all unchanged in signature and semantics.
- User-visible: an admin action that previously produced no LED pulse on some paths now produces its correct pulse on all paths. Nothing that pulsed before changes color.
- **Sequencing**: lands **first** of four. Approved order: `unify-pending-admin-action-led-mapping` → `centralize-unclaimed-device-bootstrap` → `dispatch-admin-actions-off-esp-timer` → `quiesce-poll-loops-light-sleep`. This change is a prerequisite for `dispatch-admin-actions-off-esp-timer`, which moves button-originated admin actions onto the `sdf_admin_task` path and would otherwise silently drop the `BLE_PAIRING_WINDOW` cyan pulse. It depends on nothing, and it is placed ahead of `centralize-unclaimed-device-bootstrap` because both edit `sdf_button_dispatch_action()` — doing the trivial, zero-risk edit before the one with a deadlock risk keeps the two apart.
- Out of scope: the LED colors themselves (no re-palette), the pending-action timeout, and the LED behavior after an action is *authorized* or *times out* — only the "action became pending" indication is unified here.
