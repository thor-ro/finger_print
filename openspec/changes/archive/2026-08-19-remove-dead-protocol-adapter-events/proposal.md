## Why

Both protocol adapters emit event-router events that no subscriber has ever consumed. `SDF_EVENT_ROUTER_BLE_LOCK_ACTION_COMPLETE` is emitted from `sdf_nuki_process_encrypted_custom()` on **every** successfully decrypted frame — a keyturner-states notification, a status byte, an error report — before any `command_id` dispatch, with `action_result` hardcoded to `SDF_NUKI_RESULT_OK`. `SDF_EVENT_ROUTER_ZIGBEE_COMMAND` is emitted from `sdf_zigbee_dispatch_command_event()` immediately before the direct `command_cb` call that does the actual work. A grep across non-test code finds exactly two references to each type: the enum declaration and the emit.

These are fossils of a migration that was started and abandoned. The real BLE↔Zigbee bridge is direct calls inside `sdf_app_on_message()`, and `doc/sdf_sas.md` §6.1/§6.2 still document the router-mediated design that was never built — so the architecture doc actively misleads anyone tracing lock-state flow. Meanwhile each dead emit costs a queue slot and up to `SDF_EVENT_ROUTER_EMIT_TIMEOUT_DEFAULT_MS` (100 ms) of blocking on the NimBLE host task and the Zigbee task respectively.

## What Changes

- Remove the `SDF_EVENT_ROUTER_BLE_LOCK_ACTION_COMPLETE` emit from `sdf_nuki_process_encrypted_custom()` (`sdf_protocol_ble.c:392-399`).
- Remove the `SDF_EVENT_ROUTER_ZIGBEE_COMMAND` emit from `sdf_zigbee_dispatch_command_event()` (`sdf_protocol_zigbee.c:384-391`).
- **BREAKING (compile-time only)**: remove `SDF_EVENT_ROUTER_BLE_LOCK_ACTION_COMPLETE` and `SDF_EVENT_ROUTER_ZIGBEE_COMMAND` from `sdf_event_router_type_t`, and remove the now-orphaned `sdf_event_router_ble_payload_t` / `sdf_event_router_zigbee_payload_t` structs and their union members from `sdf_event_router.h`. Renumbers the remaining enumerators; safe because no numeric value of this enum is persisted or transmitted (see Impact).
- Drop the `sdf_event_router.h` include from `sdf_protocol_ble.c` and `sdf_protocol_zigbee.c` if no other use remains.
- Correct `doc/sdf_sas.md` §6.1 (Biometric Unlock Flow) and §6.2 (Zigbee Remote Unlock Flow) to show the bridge that actually exists: `lf_on_complete() → sdf_app_update_zigbee_from_action()` for the optimistic action-derived update, and `CMD_KEYTURNER_STATES → sdf_app_map_lock_state_to_zigbee()` for the authoritative device-reported update. Both run as direct calls on the NimBLE host task, not through the router.
- Correct the event-type catalogue in `openspec/specs/sdf-services-tasks/spec.md` (§"Event Types", §"Extended Event Union"): drop the two removed types and payload members, and fix the enumerator numbering, which is **already wrong today** — the listing starts `SDF_EVENT_ROUTER_BIOMETRIC_MATCH // 0` but the header has `SDF_EVENT_ROUTER_INTERNAL_WAKE = 0` ahead of it, so every number in that block is off by one.

Explicitly **not** in scope: routing protocol-adapter traffic through the event router (the "finish the migration" alternative). That would move the Zigbee stack-lock stall onto the shared router task and head-of-line block `BIOMETRIC_MATCH`, `ENROLLMENT_*`, `ADMIN_*` and `AUDIT` dispatch. The blocking problem it was reaching for is addressed separately by the `defer-zigbee-attribute-writes` change.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

None. This change sets `skip_specs: true`.

Nothing subscribes to either event type, so removing them changes no observable behavior — no requirement in `sdf-event-router`, `sdf-services-tasks`, or any other capability describes the emits or their payloads. The `sdf-services-tasks` spec edit listed above touches only a descriptive code listing that already disagrees with the header; it corrects documentation of the enum, not a requirement about it. Following the precedent of `2026-08-15-route-service-tasks-through-platform-wdt`, no delta spec is invented to satisfy validation.

## Impact

**Code**
- `firmware/components/sdf_event_router/include/sdf_event_router.h` — 2 enumerators, 2 payload structs, 2 union members removed.
- `firmware/components/sdf_protocol_ble/src/sdf_protocol_ble.c` — emit removed from the decrypt path.
- `firmware/components/sdf_protocol_zigbee/src/sdf_protocol_zigbee.c` — emit removed from command dispatch.

**Enum renumbering safety** — verified before proposing:
- No NVS record, Zigbee/BLE wire format, or audit-log encoding stores `sdf_event_router_type_t` numerically.
- The only use outside `sdf_event_router/` is `sdf_services.c:200`, which passes a type symbolically.
- The router's internal subscriber table is indexed by the enum but is compiled from the same header, and `SDF_EVENT_ROUTER_TYPE_COUNT` adjusts automatically.

**Runtime**
- Removes one 100 ms-bounded emit per decrypted Nuki frame from the NimBLE host task.
- Removes one 100 ms-bounded emit per inbound ZCL command from the Zigbee task.
- Frees two queue slots' worth of churn; no functional change, since no callback ran.

**Tests**
- Host tests referencing either enumerator must be updated. Any test asserting on the emits is asserting on dead behavior and should be deleted rather than adapted.

**Docs**
- `doc/sdf_sas.md` §6.1, §6.2.
- `openspec/specs/sdf-services-tasks/spec.md` event-type catalogue.
