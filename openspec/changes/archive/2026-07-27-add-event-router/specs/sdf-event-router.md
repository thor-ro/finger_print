# Event Router Capability

## ADDED Requirements

### Requirement: Central event bus
The component SHALL provide a central event bus that accepts events from any source and routes them to registered subscribers.

#### Scenario: Emit and receive event
- **WHEN** a component calls `sdf_event_router_emit()` with a valid event
- **THEN** all subscribers registered for that event type SHALL receive the event synchronously

### Requirement: Priority-based dispatch
The component SHALL support four priority levels (CRITICAL, HIGH, NORMAL, LOW) and process events accordingly.

#### Scenario: Critical priority is processed first
- **WHEN** events of different priorities are emitted
- **THEN** CRITICAL events SHALL be processed before HIGH, NORMAL, and LOW events

#### Scenario: Async dispatch for non-critical
- **WHEN** an event with priority HIGH or lower is emitted via `sdf_event_router_emit_async()`
- **THEN** the event SHALL be queued and processed by the event router task

### Requirement: Subscription API
Components SHALL be able to subscribe to specific event types with optional priority filtering.

#### Scenario: Subscribe to event type
- **WHEN** a component calls `sdf_event_router_subscribe()` with a valid event type and callback
- **THEN** the callback SHALL be invoked when matching events are emitted

#### Scenario: Unsubscribe from events
- **WHEN** a component calls `sdf_event_router_unsubscribe()` with a valid handle
- **THEN** the callback SHALL no longer receive events

### Requirement: Event types
The component SHALL define the following event types: Biometric match, Zigbee command, BLE lock action, Power sleep/wake, Security lockout, Admin action, System events.

#### Scenario: Biometric match event
- **WHEN** a fingerprint match occurs
- **THEN** a `SDF_EVT_BIOMETRIC_MATCH` event SHALL be emitted with user_id and confidence

### Requirement: Integration with existing services
The event router SHALL integrate with sdf_services to emit events instead of direct callbacks.

#### Scenario: Unlock callback replaced by event
- **WHEN** a fingerprint match succeeds in sdf_services
- **THEN** `SDF_EVT_BIOMETRIC_MATCH` SHALL be emitted instead of calling `unlock_cb`

### Requirement: Audit logging
All events SHALL be logged to the audit trail when enabled.

#### Scenario: Event logged to audit
- **WHEN** `SDF_EVENT_ROUTER_ENABLE_AUDIT` is true and an event is emitted
- **THEN** the event SHALL be written to the audit log via sdf_storage