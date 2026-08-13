## Why

`sdf_event_router_event_t` is a tagged union sized by its largest member, `web_reg_auth_request_payload_t` (`username[32]` + `password_hash[32]` = 64 bytes), which makes `sizeof(sdf_event_router_event_t)` ~76 bytes — paid by *every* event of *every* type, copied into the router's central queue and into each of the four per-task queues (button/admin/enroll/match) that also store the full struct by value. Investigation found the web-registration credential path is worse than "big union member": `sdf_ble_companion_on_auth_request()` builds an event carrying the raw username + password hash and emits it through the router purely to reach `sdf_app_on_web_reg_auth_request()`, which immediately copies both fields into `sdf_services`' already-existing owned single-slot buffer (`request_web_username` / `request_web_password_hash`, gated by `web_reg_auth_pending`) — meaning the hash sits in a FreeRTOS queue for no reason once it has that owned home. Separately, `SDF_EVENT_ROUTER_WEB_REG_AUTH_RESULT`'s payload (`username` + `permission`) is a pure round-trip: `sdf_services.c` reads both fields out of that same owned slot to build the event, and the sole consumer (`sdf_app_on_web_reg_auth_result`) reads them straight back out — no new information crosses the event.

Both request and result payloads can be eliminated rather than merely shrunk, following a pattern the codebase already uses for the three sibling BLE-triggered admin actions (NUKI_REPAIR, ENROLL_ADMIN, ZB_JOIN): `sdf_app_on_ble_admin_action_request()` calls `sdf_services_request_admin_action()` directly from the NimBLE host task, with no event router hop at all.

## What Changes

- **BREAKING** (internal API only, no external behavior change): Remove the `SDF_EVENT_ROUTER_WEB_REG_AUTH_REQUEST` event type and its `web_reg_auth_request_payload_t` union member entirely. `sdf_ble_companion_on_auth_request()` (in `sdf_app.c`) calls `sdf_services_set_web_reg_auth()` and `sdf_services_request_admin_action()` directly and synchronously from the NimBLE host task — the same pattern already used by `sdf_app_on_ble_admin_action_request()` for NUKI_REPAIR/ENROLL_ADMIN/ZB_JOIN. The raw password hash never enters any FreeRTOS queue; it is written once into `sdf_services`' existing owned buffer.
- Shrink `SDF_EVENT_ROUTER_WEB_REG_AUTH_RESULT`'s payload (`web_reg_auth_result_payload_t`): drop `username` and `permission`, both of which duplicate state the sole consumer can already read via the existing `sdf_services_get_web_reg_auth()` accessor. `sdf_app_on_web_reg_auth_result()` calls that accessor instead of reading the fields off the event.
- As a result, `sdf_event_router_event_t`'s union is no longer dominated by credential-sized payloads; its largest remaining member becomes `sdf_event_router_audit_payload_t` (16 bytes), shrinking `sizeof(sdf_event_router_event_t)` from ~76 bytes to ~28 bytes — a cut applied automatically to the router's central queue and all four per-task queues (button/admin/enroll/match) that store the struct by value.
- Remove the now-dead `SDF_EVENT_ROUTER_WEB_REG_AUTH_REQUEST` subscription and handler (`sdf_app_on_web_reg_auth_request`) from `sdf_app.c`.

## Capabilities

### New Capabilities
(none)

### Modified Capabilities
- `sdf-services-tasks`: The "Web Registration Authorization" requirement gains a credential-transport constraint — raw web-registration credential material (username, password hash) SHALL never be carried in an `sdf_event_router_event_t` payload or copied into any event-router queue; it SHALL be written directly into `sdf_services`' owned pending-request state and read back exclusively via its existing accessors.

## Impact

- `firmware/components/sdf_event_router/include/sdf_event_router.h`: remove `SDF_EVENT_ROUTER_WEB_REG_AUTH_REQUEST` from `sdf_event_router_type_t`, remove `sdf_event_router_web_reg_auth_request_payload_t` and its union member, shrink `sdf_event_router_web_reg_auth_result_payload_t`.
- `firmware/components/sdf_app/src/sdf_app.c`: `sdf_ble_companion_on_auth_request()` calls `sdf_services_set_web_reg_auth()` + `sdf_services_request_admin_action()` directly; remove the `WEB_REG_AUTH_REQUEST` subscription and `sdf_app_on_web_reg_auth_request()`; update `sdf_app_on_web_reg_auth_result()` to fetch username/permission via `sdf_services_get_web_reg_auth()`.
- No changes expected to `sdf_services.c`'s accessor functions (`sdf_services_set_web_reg_auth`, `_get_web_reg_auth`, `_get_web_reg_password_hash`, `_clear_web_reg_auth`) or to `sdf_ble_companion.c` — same callback signatures, same GATT semantics.
- Test impact: `firmware/components/sdf_event_router/test/test_sdf_event_router.c` and `firmware/components/sdf_services/test/test_sdf_services.c` — remove/update any coverage keyed on the removed event type or payload fields; `firmware/test_runner/main/test_runner_main.c` if it references the removed symbols.
