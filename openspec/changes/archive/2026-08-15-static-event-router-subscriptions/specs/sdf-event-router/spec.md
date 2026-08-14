## MODIFIED Requirements

### Requirement: Router initialization is idempotent
`sdf_event_router_init()` SHALL create the router's queue and subscriber table on first call, and SHALL return `ESP_OK` without recreating them on subsequent calls. Initialization SHALL NOT create the dispatch task; the dispatch task is created by `sdf_event_router_start()` so that subscribers can register against a router that is not yet dispatching.

#### Scenario: First init creates router resources
- **WHEN** `sdf_event_router_init()` is called and the router has not been initialized
- **THEN** it creates the dispatch queue and an empty subscriber table, and returns `ESP_OK`
- **AND** no dispatch task is created

#### Scenario: Repeated init is a no-op
- **WHEN** `sdf_event_router_init()` is called again after a successful init
- **THEN** it returns `ESP_OK` without creating new queue or table resources and without discarding already-registered subscribers

### Requirement: Emit routes by priority through a single dispatch path
`sdf_event_router_emit()` SHALL validate event pointer, initialization state, and event type (rejecting `SDF_EVENT_ROUTER_INTERNAL_WAKE` and out-of-range types with `ESP_ERR_INVALID_ARG`), SHALL dispatch `SDF_EVENT_ROUTER_PRIO_CRITICAL` events synchronously on the caller's context, and SHALL queue all other events for asynchronous dispatch by the router task. This is the router's one and only emit entry point — there SHALL NOT be a second function offering different or unspecified semantics for the same operation. Emitting between `sdf_event_router_init()` and `sdf_event_router_start()` SHALL be permitted: non-critical events are queued and delivered once the dispatch task starts.

#### Scenario: Critical event dispatches synchronously
- **WHEN** `sdf_event_router_emit()` is called with a `PRIO_CRITICAL` event
- **THEN** matching subscribers are invoked before `sdf_event_router_emit()` returns

#### Scenario: Non-critical event is queued
- **WHEN** `sdf_event_router_emit()` is called with a non-`PRIO_CRITICAL` event
- **THEN** the event is enqueued for the router task and `sdf_event_router_emit()` returns without waiting for dispatch

#### Scenario: Full queue drops the event
- **WHEN** `sdf_event_router_emit()` is called with a non-`PRIO_CRITICAL` event and the queue is full for the send timeout
- **THEN** it returns `ESP_ERR_NO_MEM` and the event is not delivered

#### Scenario: Emit before init is rejected
- **WHEN** `sdf_event_router_emit()` is called before `sdf_event_router_init()` has completed successfully
- **THEN** it returns `ESP_ERR_INVALID_ARG`

#### Scenario: Non-critical emit before start is delivered after start
- **WHEN** `sdf_event_router_emit()` is called with a non-`PRIO_CRITICAL` event after `sdf_event_router_init()` but before `sdf_event_router_start()`
- **THEN** the event is enqueued, and it is dispatched to matching subscribers once `sdf_event_router_start()` has created the dispatch task

#### Scenario: Internal wake or invalid type emit is rejected
- **WHEN** `sdf_event_router_emit()` is called with `event->type == SDF_EVENT_ROUTER_INTERNAL_WAKE` or `event->type >= SDF_EVENT_ROUTER_TYPE_COUNT`
- **THEN** it returns `ESP_ERR_INVALID_ARG`

## ADDED Requirements

### Requirement: Subscribe registers a permanent subscriber before start
`sdf_event_router_subscribe()` SHALL take the event type, minimum accepted priority, callback, and callback context, and SHALL NOT return a subscriber handle — with `unsubscribe()` removed there is no operation a handle could be used for. It SHALL reject a `NULL` callback, SHALL reject `SDF_EVENT_ROUTER_INTERNAL_WAKE` as an internal queue-only sentinel, SHALL reject a `type` outside the valid `sdf_event_router_type_t` range, SHALL reject any call made after `sdf_event_router_start()`, SHALL reject a call that would exceed the router's fixed subscriber capacity, and SHALL otherwise register the subscriber against that type so it is included in future dispatch for events of that type. Registration SHALL NOT allocate from the heap.

#### Scenario: NULL callback is rejected
- **WHEN** `sdf_event_router_subscribe()` is called with `cb == NULL`
- **THEN** it returns `ESP_ERR_INVALID_ARG` and does not register a subscriber

#### Scenario: Internal wake sentinel is rejected
- **WHEN** `sdf_event_router_subscribe()` is called with `type == SDF_EVENT_ROUTER_INTERNAL_WAKE`
- **THEN** it returns `ESP_ERR_INVALID_ARG` and does not register a subscriber

#### Scenario: Out-of-range event type is rejected
- **WHEN** `sdf_event_router_subscribe()` is called with `type >= SDF_EVENT_ROUTER_TYPE_COUNT`
- **THEN** it returns `ESP_ERR_INVALID_ARG` and does not index or write to the subscriber table

#### Scenario: Subscription after start is rejected
- **WHEN** `sdf_event_router_subscribe()` is called after `sdf_event_router_start()` has returned `ESP_OK`
- **THEN** it returns `ESP_ERR_INVALID_STATE` and does not modify the subscriber table

#### Scenario: Exhausted subscriber capacity is rejected
- **WHEN** `sdf_event_router_subscribe()` is called and the router's fixed subscriber capacity is already fully occupied
- **THEN** it returns `ESP_ERR_NO_MEM` and does not register a subscriber

#### Scenario: Valid subscription is dispatched matching events
- **WHEN** `sdf_event_router_subscribe()` is called before `sdf_event_router_start()` with a valid `type` and non-`NULL` callback
- **THEN** it returns `ESP_OK`, and subsequent emits of that `type` at or below the subscriber's `min_prio` invoke the callback

### Requirement: Start freezes the subscriber table
`sdf_event_router_start()` SHALL create the dispatch task and SHALL mark the subscriber table as frozen. After a successful `start()`, the set of subscribers SHALL NOT change for the remainder of the boot. Calling `sdf_event_router_start()` before `sdf_event_router_init()` SHALL return `ESP_ERR_INVALID_STATE`, and calling it more than once SHALL return `ESP_OK` without creating a second dispatch task.

#### Scenario: Start creates the dispatch task
- **WHEN** `sdf_event_router_start()` is called after a successful `sdf_event_router_init()`
- **THEN** it creates the dispatch task and returns `ESP_OK`
- **AND** events already queued before the call are dispatched

#### Scenario: Start before init is rejected
- **WHEN** `sdf_event_router_start()` is called before `sdf_event_router_init()` has completed successfully
- **THEN** it returns `ESP_ERR_INVALID_STATE` and no dispatch task is created

#### Scenario: Repeated start is a no-op
- **WHEN** `sdf_event_router_start()` is called again after a successful start
- **THEN** it returns `ESP_OK` without creating a second dispatch task

### Requirement: Registration completes on a single context before dispatch begins
All subscriber registration SHALL complete on one execution context before `sdf_event_router_start()` is called, so that no registration can run concurrently with a dispatch. The system SHALL NOT register subscribers from within a task that is started before `sdf_event_router_start()`.

#### Scenario: Subscribers register before tasks are created
- **WHEN** the firmware brings up a subsystem that consumes router events
- **THEN** that subsystem's subscriptions are registered before any task belonging to it is created, and before `sdf_event_router_start()` is called

#### Scenario: Late registration fails loudly
- **WHEN** a subsystem attempts to register a subscription after `sdf_event_router_start()`
- **THEN** the call returns `ESP_ERR_INVALID_STATE` and the failure is logged rather than being silently ignored

### Requirement: Dispatch runs without acquiring a lock
Because the subscriber table is frozen before dispatch begins, dispatch SHALL iterate the subscribers for an event type directly, without acquiring a lock and without copying subscribers into an intermediate buffer. Dispatch SHALL NOT drop an event due to lock contention, and SHALL NOT impose a runtime cap on the number of subscribers invoked for one event.

#### Scenario: Every matching subscriber is invoked
- **WHEN** an event is dispatched and multiple subscribers are registered for its type at an accepting `min_prio`
- **THEN** every one of those subscribers' callbacks is invoked, with no runtime fan-out limit truncating the set

#### Scenario: Reentrant critical emit from a callback is safe
- **WHEN** a subscriber callback calls `sdf_event_router_emit()` with a `PRIO_CRITICAL` event during dispatch
- **THEN** the nested dispatch completes normally without deadlock and without the event being dropped

### Requirement: Subscriber capacity is declared per component and verified at startup
The router's subscriber capacity SHALL be derived from per-component subscription counts declared in a single shared header, with a build-time assertion that the pool is large enough for the declared total. Because a component can add a subscription without updating its declared count, `sdf_event_router_start()` SHALL additionally verify that no registration was rejected during startup, and SHALL fail rather than continue with a silently incomplete subscriber set.

#### Scenario: Declared total exceeding the pool fails the build
- **WHEN** the sum of the declared per-component subscription counts exceeds the router's subscriber pool size
- **THEN** the build fails with a compile-time assertion identifying the capacity as the cause

#### Scenario: Rejected registration fails startup
- **WHEN** any `sdf_event_router_subscribe()` call was rejected for lack of capacity before `sdf_event_router_start()` is called
- **THEN** `sdf_event_router_start()` returns an error and logs the shortfall, rather than starting dispatch with a subscriber set that is missing entries

#### Scenario: Capacity utilisation is observable
- **WHEN** `sdf_event_router_start()` succeeds
- **THEN** it logs the number of registered subscribers against the configured capacity, so headroom is visible in boot logs

### Requirement: Priority filter admits events at or below the subscriber's minimum importance
A subscriber's `min_prio` SHALL mean the *lowest importance* the subscriber accepts, evaluated as `min_prio >= event->priority` over an enum in which `SDF_EVENT_ROUTER_PRIO_CRITICAL` is the most important value and `SDF_EVENT_ROUTER_PRIO_LOW` the least. The public header SHALL document this ordering, including that subscribing with `PRIO_CRITICAL` admits only critical events rather than "critical and above".

#### Scenario: Low minimum accepts every priority
- **WHEN** a subscriber registers with `min_prio == SDF_EVENT_ROUTER_PRIO_LOW`
- **THEN** it receives events of that type at CRITICAL, HIGH, NORMAL, and LOW priority

#### Scenario: Critical minimum accepts only critical events
- **WHEN** a subscriber registers with `min_prio == SDF_EVENT_ROUTER_PRIO_CRITICAL`
- **THEN** it receives only `PRIO_CRITICAL` events of that type, and events of the same type emitted at HIGH, NORMAL, or LOW priority are not delivered to it

## REMOVED Requirements

### Requirement: Subscribe validates arguments and registers by event type

**Reason**: Replaced by "Subscribe registers a permanent subscriber before start". The argument validation carries over unchanged, but the requirement's contract changes shape in two ways that cannot be expressed as a scenario-preserving edit: the `sdf_event_router_subscriber_t **handle` out-parameter is gone (so the "NULL handle output is rejected" scenario no longer describes anything), and registration is now bounded by a fixed capacity and permitted only before `sdf_event_router_start()`.

**Migration**: Drop the trailing `&handle` argument from every `sdf_event_router_subscribe()` call and delete the handle variables. Ensure the call happens before `sdf_event_router_start()`; a subscription registered later now fails with `ESP_ERR_INVALID_STATE` instead of silently racing dispatch.

### Requirement: Unsubscribe removes a subscriber safely

**Reason**: No caller needs it. Every `sdf_event_router_unsubscribe()` call site in the firmware is either unreachable (`sdf_services_stop_tasks()` and `sdf_ble_companion_deinit()` have no callers) or executes immediately before `vTaskDelete()`, and device teardown goes through `esp_restart()`. Keeping the operation forced a heap-allocated subscriber list, a dispatch lock, and a snapshot buffer, and its contract was unsound: it returned `ESP_OK` while callbacks belonging to the removed subscriber could still be in flight, with no barrier the caller could rely on before freeing the associated context.

**Migration**: Remove all `sdf_event_router_unsubscribe()` calls. Subscriptions now live for the lifetime of the boot; a subsystem that must stop reacting to events SHALL gate on its own state inside its callback rather than deregistering. Code that tore down subscriptions as part of a shutdown path drops that step entirely.
