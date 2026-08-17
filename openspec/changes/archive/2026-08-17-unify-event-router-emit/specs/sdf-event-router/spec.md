## ADDED Requirements

### Requirement: Emit enqueues every event through a single dispatch path
`sdf_event_router_emit()` SHALL take the event and an explicit send timeout in milliseconds, and SHALL validate event pointer, initialization state, and event type (rejecting `SDF_EVENT_ROUTER_INTERNAL_WAKE` and out-of-range types with `ESP_ERR_INVALID_ARG`). It SHALL enqueue every event for dispatch by the router task regardless of priority, placing `SDF_EVENT_ROUTER_PRIO_CRITICAL` events ahead of events already waiting in the queue and all other priorities behind them. It SHALL NOT invoke any subscriber callback on the caller's context. This is the router's one and only emit entry point — there SHALL NOT be a second function offering different or unspecified semantics for the same operation, and in particular the send timeout SHALL be expressed as a parameter rather than as a separate non-blocking entry point. Emitting between `sdf_event_router_init()` and `sdf_event_router_start()` SHALL be permitted at every priority: events are queued and delivered once the dispatch task starts.

#### Scenario: Critical event is dispatched on the router task
- **WHEN** `sdf_event_router_emit()` is called with a `PRIO_CRITICAL` event
- **THEN** matching subscribers are invoked on the router's dispatch task and never on the emitting task
- **AND** whether dispatch has completed by the time `sdf_event_router_emit()` returns depends on the relative scheduling priority of the two tasks and is not guaranteed either way

#### Scenario: Critical event is dispatched ahead of queued non-critical events
- **WHEN** non-critical events are already waiting in the queue and `sdf_event_router_emit()` is called with a `PRIO_CRITICAL` event
- **THEN** the critical event is dispatched before those already-queued non-critical events

#### Scenario: Non-critical events are dispatched in arrival order
- **WHEN** `sdf_event_router_emit()` is called with two non-`PRIO_CRITICAL` events in sequence
- **THEN** both are enqueued behind any events already waiting, and are dispatched in the order they were emitted

#### Scenario: Zero timeout never blocks the caller
- **WHEN** `sdf_event_router_emit()` is called with a send timeout of `0`
- **THEN** it returns without blocking the calling context, enqueuing the event if space is available and reporting `ESP_ERR_NO_MEM` if it is not

#### Scenario: Full queue drops the event
- **WHEN** `sdf_event_router_emit()` is called and the queue remains full for the supplied send timeout
- **THEN** it returns `ESP_ERR_NO_MEM` and the event is not delivered, at every priority including `PRIO_CRITICAL`

#### Scenario: Emit before init is rejected
- **WHEN** `sdf_event_router_emit()` is called before `sdf_event_router_init()` has completed successfully
- **THEN** it returns `ESP_ERR_INVALID_ARG`

#### Scenario: Emit before start is delivered after start
- **WHEN** `sdf_event_router_emit()` is called at any priority after `sdf_event_router_init()` but before `sdf_event_router_start()`
- **THEN** the event is enqueued, and it is dispatched to matching subscribers once `sdf_event_router_start()` has created the dispatch task

#### Scenario: Internal wake or invalid type emit is rejected
- **WHEN** `sdf_event_router_emit()` is called with `event->type == SDF_EVENT_ROUTER_INTERNAL_WAKE` or `event->type >= SDF_EVENT_ROUTER_TYPE_COUNT`
- **THEN** it returns `ESP_ERR_INVALID_ARG`

## MODIFIED Requirements

### Requirement: Dispatch runs without acquiring a lock
Because the subscriber table is frozen before dispatch begins, dispatch SHALL iterate the subscribers for an event type directly, without acquiring a lock and without copying subscribers into an intermediate buffer. Dispatch SHALL NOT drop an event due to lock contention, and SHALL NOT impose a runtime cap on the number of subscribers invoked for one event. Dispatch SHALL run only on the router's dispatch task, so subscriber callbacks never execute on an emitting task's stack and are never nested inside one another.

#### Scenario: Every matching subscriber is invoked
- **WHEN** an event is dispatched and multiple subscribers are registered for its type at an accepting `min_prio`
- **THEN** every one of those subscribers' callbacks is invoked, with no runtime fan-out limit truncating the set

#### Scenario: Reentrant critical emit from a callback is safe
- **WHEN** a subscriber callback calls `sdf_event_router_emit()` with a `PRIO_CRITICAL` event during dispatch
- **THEN** the event is enqueued rather than dispatched inline, the calling callback returns before the new event is dispatched, and the new event is subsequently dispatched by the router task without deadlock and without being dropped

#### Scenario: Callbacks do not run on an emitter's stack
- **WHEN** any task emits an event of any priority
- **THEN** no subscriber callback executes on that task's stack, so the emitting task does not inherit a callback's blocking behaviour or stack usage

## REMOVED Requirements

### Requirement: Emit routes by priority through a single dispatch path
**Reason**: Replaced by "Emit enqueues every event through a single dispatch path". Synchronous dispatch of `PRIO_CRITICAL` events on the caller's context ran subscriber callbacks — including calls that block for up to 1.25 s acquiring Zigbee locks — on whichever task happened to emit. The send timeout, previously fixed at 100 ms and duplicated as a separate non-blocking entry point, becomes an explicit parameter.

**Migration**: `sdf_event_router_emit(&event)` becomes `sdf_event_router_emit(&event, 100)` to preserve the previously implicit timeout. `sdf_event_router_emit_nonblocking(&event)` becomes `sdf_event_router_emit(&event, 0)`. Callers that relied on a `PRIO_CRITICAL` emit having completed dispatch by the time it returned must instead observe the effect asynchronously; no such caller exists today.
