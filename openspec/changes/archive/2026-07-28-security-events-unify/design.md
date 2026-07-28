## Context

The smart_door firmware currently has two paths for security event emission:
1. The event router (`sdf_event_router_emit()`) — a centralized async dispatch mechanism with subscriber priorities and queue-based delivery
2. Legacy direct callbacks in `sdf_app.c` (`sdf_app_set_event_callback()`, `sdf_app_set_audit_callback()`) — synchronous callback invocation

The existing `security-event-emission` spec already mandates that security events flow through the event router, but the legacy callback infrastructure still exists in `sdf_app.c` and is used by non-security event paths (e.g., BLE pairing, Zigbee protocol errors). This creates a risk of duplicate audit entries when both paths fire, and makes the security event flow harder to reason about.

The codebase targets ESP32-C6 with ESP-IDF v5.5.3, uses FreeRTOS for tasking, and NimBLE for BLE. The event router runs at priority 5 with a 3072-word stack and a configurable queue depth.

## Goals / Non-Goals

**Goals:**
- All security events (match success, match failure, lockout enter/clear) flow exclusively through `sdf_event_router_emit()` with no legacy callback path
- Exactly one audit log entry per security event — no duplicates from legacy callback paths
- `sdf_services_emit_security_event()` becomes the single source of truth for security event emission
- Subscribers (sdf_app_on_event, sdf_audit, etc.) receive events through the event router only

**Non-Goals:**
- Refactoring non-security event paths (BLE pairing events, Zigbee protocol events) — those may still use legacy callbacks if desired
- Removing the callback API from sdf_app entirely — only the security event subscription/dispatch path is eliminated
- Changing the event router's internal dispatch mechanism
- Modifying the fingerprint sensor driver or hardware abstraction layer

## Decisions

**Decision 1: Remove `sdf_app_set_event_callback` and `sdf_app_set_audit_callback` entirely**
- Rationale: These callbacks are the legacy path that causes duplicate audit entries. Removing them forces all consumers through the event router, which is the intended architecture per the security-event-emission spec.
- Alternative: Keep both paths but add deduplication logic — rejected because it adds complexity and still leaves two code paths to maintain.

**Decision 2: Introduce `SDF_EVENT_ROUTER_AUDIT` event type for audit events**
- Rationale: Instead of calling `sdf_app_emit_audit()` directly (which goes through the legacy callback), audit events for security operations should be emitted as events through the router. This ensures subscribers receive them consistently.
- Alternative: Keep audit as a separate direct call — rejected because it creates a second path alongside event-router-delivered events.

**Decision 3: `sdf_app_on_event()` remains the single event handler for sdf_app**
- Rationale: The app layer's event handler already subscribes to security events via the event router. No architectural change to the subscription pattern — only the legacy callback registration is removed.
- Alternative: Create a separate audit handler module — rejected because sdf_app already handles alarm mask and audit logic correctly.

**Decision 4: `sdf_services_emit_security_event()` is the sole entry point for security events**
- Rationale: Centralizes security event construction and emission in one function, making it easy to audit what events are produced and in what priority.
- Alternative: Allow individual task modules to emit security events directly — rejected because it would scatter event construction logic across multiple files.

## Risks / Trade-offs

- [Risk] Removing legacy callbacks may break any external code that directly registers callbacks with sdf_app — Mitigation: The callbacks are internal to sdf_app and not part of any public header API; verify no external consumers exist.
- [Risk] `SDF_EVENT_ROUTER_AUDIT` event type increases the enum and payload struct size — Mitigation: The event router payload union already has room; audit events share the existing `security` payload shape.
- [Risk] Event queue overflow for high-frequency security events (rapid failed attempts) — Mitigation: Critical priority events (lockout enter) are dispatched synchronously; non-critical events use the queue with drop-on-full semantics that were already in place.