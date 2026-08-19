## Context

See proposal.md — Why. Design-relevant facts established while verifying the removal is safe:

- `sdf_event_router_type_t` is used as a direct index into the router's fixed subscriber table. That table is compiled from the same header, and `SDF_EVENT_ROUTER_TYPE_COUNT` is the sentinel that sizes it — so renumbering is self-consistent within the component.
- The only reference to the enum type outside `sdf_event_router/` is `sdf_services.c:200` (`sdf_services_emit_enrollment_event`), which takes a type as a parameter and never assumes a numeric value.
- Grep across the whole `firmware/` tree, tests included, finds seven lines touching the two doomed types or their payload members — all of them in the enum declaration and the two emit sites. **No test asserts on either event.** Removal is a pure deletion with no test rewrites.
- The union's size is set by `sdf_event_router_audit_payload_t` (16 bytes), per the comment at `sdf_event_router.h:167`. Both payloads being removed are smaller than that, so `sizeof(sdf_event_router_event_t)` does not change.

## Goals / Non-Goals

**Goals:**
- Delete the two dead emits and their enum/payload declarations without changing any observable behavior.
- Bring `doc/sdf_sas.md` and the `sdf-services-tasks` event-type catalogue into agreement with the header.
- Leave the event router's public API contract (`init`/`subscribe`/`start`/`emit`) untouched.

**Non-Goals:**
- Any change to how BLE lock state reaches Zigbee. The direct-call bridge in `sdf_app_on_message()` stays exactly as it is.
- Any change to the blocking behavior of `sdf_protocol_zigbee_update_lock_state()`. That is the `defer-zigbee-attribute-writes` change; the two are independent and can land in either order.
- Auditing the remaining event types for subscribers. If other fossils exist, they are out of scope here.

## Decisions

### D1: Delete the enumerators rather than keep them as reserved placeholders

**Chosen:** remove `SDF_EVENT_ROUTER_BLE_LOCK_ACTION_COMPLETE` and `SDF_EVENT_ROUTER_ZIGBEE_COMMAND` outright, letting the remaining enumerators renumber.

**Alternative considered:** keep them with a `/* reserved, unused */` comment to preserve numbering.

**Rationale:** preserving numbering only matters if a number crosses a persistence or protocol boundary, and none does (verified — see Context and proposal.md Impact). A reserved placeholder still occupies a subscriber-table slot and still invites the next reader to wonder what consumes it, which is exactly the confusion being removed. Deleting is cheaper and more honest.

### D2: Remove the payload structs, not just the union members

**Chosen:** delete `sdf_event_router_ble_payload_t` and `sdf_event_router_zigbee_payload_t` entirely.

**Rationale:** with no event type carrying them they have no meaning. Leaving the typedefs behind would let a future emit reintroduce the fossil without anyone noticing. Neither is referenced outside the union.

Note for the follow-on change: `sdf_event_router_zigbee_payload_t` is *not* what a future Zigbee bridge would want anyway — it carries `command_id`/`user_id`, and a Door Lock cluster update needs `LockState`. Its removal does not foreclose anything real.

### D3: Correct the SAS diagrams to the direct-call reality rather than deleting them

**Chosen:** rewrite the `BLE -> EVT : emit(...)` / `EVT -> APP` hops in §6.1 and §6.2 to show `BLE -> APP : message_cb` and the two distinct Zigbee update paths.

**Alternative considered:** delete the Zigbee-update portion of the diagrams.

**Rationale:** the flows themselves are real and useful — a reader tracing "how does HA learn the door is unlocked" needs them. Only the router hop is fiction. §6.2 in particular must show both paths, because they differ in trust: `update_zigbee_from_action()` is optimistic and derived from the requested action, while the `KEYTURNER_STATES` path is authoritative and device-reported. Collapsing them would replace one inaccuracy with another.

### D4: `skip_specs: true` rather than a synthesized delta

See proposal.md — Capabilities. No requirement in any capability describes these emits, and nothing subscribes, so there is no behavior to re-specify. The `sdf-services-tasks` edit corrects a descriptive listing that is already wrong today (off-by-one from the missing `INTERNAL_WAKE = 0`), which is a documentation fix, not a requirement change.

**Trade-off accepted:** this means `openspec/specs/sdf-services-tasks/spec.md` is edited directly rather than through the archive/sync flow. That is the right call for a listing that has drifted from the header, but it is a deliberate exception worth noting in review.

## Risks / Trade-offs

- **A future change wants a real BLE→Zigbee event and has to re-add an enumerator** → Cheap. Re-adding an enumerator is a one-line change, and the payload it needs (`LockState`) differs from the one being deleted, so nothing is actually lost.
- **An out-of-tree or WIP branch subscribes to one of these types and fails to compile** → This is the desired outcome: a compile error is a loud, immediate signal, far better than the silent no-op subscription that exists today. Worth a heads-up in the commit message.
- **Enum renumbering breaks something the grep missed** → Mitigated by the verification in Context (no persistence, no wire format, one symbolic external use). A full `rtk cargo`-equivalent firmware build plus the host test suite will catch any missed compile-time dependency; there is no runtime-only dependency path, since nothing serializes the value.
- **The SAS rewrite introduces a *new* inaccuracy** → Mitigated by deriving the corrected diagrams from the actual call sites (`sdf_app.c:1383`, `1398`, `1294-1297`, `239-259`) rather than from memory, and by keeping the optimistic/authoritative distinction explicit.

## Migration Plan

No runtime migration, no persisted state, no OTA compatibility concern — the removed values never left the device. Deploy is an ordinary firmware build.

Rollback is `git revert`; because nothing consumed the events, a device running pre-change and post-change firmware behaves identically.

Recommended landing order relative to `defer-zigbee-attribute-writes`: **this change first.** It is pure deletion and touches `sdf_protocol_zigbee.c` only to remove lines, minimizing conflict with the follow-on change's edits to the same file.
