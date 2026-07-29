## ADDED Requirements

### Requirement: Event subscribers are indexed by type for O(1) dispatch
The event router SHALL index subscribers by `sdf_event_router_type_t` instead of scanning a flat linked list on every emit.

#### Scenario: Emit dispatches only to matching type subscribers
- **WHEN** `sdf_event_router_emit()` is called with an event of type T
- **THEN** the router looks up the subscriber list for type T directly
- **AND** only subscribers registered for type T are evaluated (no iteration over unrelated types)

#### Scenario: Adding a subscriber updates the type index
- **WHEN** `sdf_event_router_subscribe()` is called with type T
- **THEN** the new subscriber is added to the type T list in the index
- **AND** subsequent emits of type T will include this subscriber

#### Scenario: Removing a subscriber updates the type index
- **WHEN** `sdf_event_router_unsubscribe()` is called with a subscriber handle
- **THEN** the subscriber is removed from its type's list in the index
- **AND** subsequent emits of that type will not include this subscriber

### Requirement: emit_async respects priority for critical events
The `sdf_event_router_emit_async()` function SHALL respect event priority, routing `PRIO_CRITICAL` events through the synchronous dispatch path.

#### Scenario: Critical event sent via emit_async dispatches synchronously
- **WHEN** `sdf_event_router_emit_async()` is called with a `PRIO_CRITICAL` event
- **THEN** the event is dispatched synchronously (not queued)
- **AND** all critical-priority subscribers for that event type are invoked immediately

#### Scenario: Non-critical event sent via emit_async is queued
- **WHEN** `sdf_event_router_emit_async()` is called with a non-critical event
- **THEN** the event is queued for asynchronous dispatch by the event router task

## MODIFIED Requirements

### Requirement: Event router type enum includes battery event type
The `sdf_event_router_type_t` enum SHALL include a `SDF_EVENT_ROUTER_POWER_BATTERY` value for battery-level update events.

#### Scenario: Battery event is a distinct event type
- **WHEN** a battery level update occurs
- **THEN** an event of type `SDF_EVENT_ROUTER_POWER_BATTERY` is emitted (not `SDF_EVENT_ROUTER_POWER_SLEEP`)
- **AND** subscribers for `SDF_EVENT_ROUTER_POWER_BATTERY` receive it independently of sleep event subscribers