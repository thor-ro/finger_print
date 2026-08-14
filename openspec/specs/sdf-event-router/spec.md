# SDF Event Router

## Purpose
Specifies the public API contract for `sdf_event_router` — the pub/sub component tasks and drivers use to exchange typed events (init, subscribe, unsubscribe, emit, and internal dispatch validation).

## Requirements

### Requirement: Router initialization is idempotent
`sdf_event_router_init()` SHALL create the router's queue, dispatch task, and lock on first call, and SHALL return `ESP_OK` without recreating any of them on subsequent calls.

#### Scenario: First init creates router resources
- **WHEN** `sdf_event_router_init()` is called and the router has not been initialized
- **THEN** it creates the dispatch queue, dispatch task, and lock, and returns `ESP_OK`

#### Scenario: Repeated init is a no-op
- **WHEN** `sdf_event_router_init()` is called again after a successful init
- **THEN** it returns `ESP_OK` without creating new queue, task, or lock resources

### Requirement: Subscribe validates arguments and registers by event type
`sdf_event_router_subscribe()` SHALL reject a `NULL` callback or `NULL` handle output pointer, SHALL reject `SDF_EVENT_ROUTER_INTERNAL_WAKE` as an internal queue-only sentinel, SHALL reject a `type` outside the valid `sdf_event_router_type_t` range, and SHALL otherwise register the subscriber against that type so it is included in future dispatch for events of that type.

#### Scenario: NULL callback is rejected
- **WHEN** `sdf_event_router_subscribe()` is called with `cb == NULL`
- **THEN** it returns `ESP_ERR_INVALID_ARG` and does not register a subscriber

#### Scenario: NULL handle output is rejected
- **WHEN** `sdf_event_router_subscribe()` is called with `handle == NULL`
- **THEN** it returns `ESP_ERR_INVALID_ARG` and does not register a subscriber

#### Scenario: Internal wake sentinel is rejected
- **WHEN** `sdf_event_router_subscribe()` is called with `type == SDF_EVENT_ROUTER_INTERNAL_WAKE`
- **THEN** it returns `ESP_ERR_INVALID_ARG` and does not register a subscriber

#### Scenario: Out-of-range event type is rejected
- **WHEN** `sdf_event_router_subscribe()` is called with `type >= SDF_EVENT_ROUTER_TYPE_COUNT`
- **THEN** it returns `ESP_ERR_INVALID_ARG` and does not index or write to the subscriber table

#### Scenario: Valid subscription is dispatched matching events
- **WHEN** `sdf_event_router_subscribe()` is called with a valid `type`, non-`NULL` callback, and non-`NULL` handle
- **THEN** it returns `ESP_OK`, populates `*handle` with a subscriber handle, and subsequent emits of that `type` at or below the subscriber's `min_prio` invoke the callback

### Requirement: Unsubscribe removes a subscriber safely
`sdf_event_router_unsubscribe()` SHALL remove the given subscriber from its type's list so it no longer receives dispatched events, and SHALL do so without a dispatch in progress observing a freed subscriber node.

#### Scenario: Unsubscribed handle stops receiving events
- **WHEN** `sdf_event_router_unsubscribe()` is called with a handle from a prior successful `subscribe()`
- **THEN** it returns `ESP_OK`, and subsequent emits of that subscriber's type no longer invoke its callback

#### Scenario: Unknown handle is rejected
- **WHEN** `sdf_event_router_unsubscribe()` is called with a handle not currently registered
- **THEN** it returns `ESP_ERR_NOT_FOUND`

#### Scenario: NULL handle is rejected
- **WHEN** `sdf_event_router_unsubscribe()` is called with `handle == NULL`
- **THEN** it returns `ESP_ERR_INVALID_ARG`

### Requirement: Emit routes by priority through a single dispatch path
`sdf_event_router_emit()` SHALL validate event pointer, initialization state, and event type (rejecting `SDF_EVENT_ROUTER_INTERNAL_WAKE` and out-of-range types with `ESP_ERR_INVALID_ARG`), SHALL dispatch `SDF_EVENT_ROUTER_PRIO_CRITICAL` events synchronously on the caller's context, and SHALL queue all other events for asynchronous dispatch by the router task. This is the router's one and only emit entry point — there SHALL NOT be a second function offering different or unspecified semantics for the same operation.

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

#### Scenario: Internal wake or invalid type emit is rejected
- **WHEN** `sdf_event_router_emit()` is called with `event->type == SDF_EVENT_ROUTER_INTERNAL_WAKE` or `event->type >= SDF_EVENT_ROUTER_TYPE_COUNT`
- **THEN** it returns `ESP_ERR_INVALID_ARG`

### Requirement: Dispatch validates event type before indexing subscribers
The router's internal dispatch path SHALL validate `event->type != SDF_EVENT_ROUTER_INTERNAL_WAKE && event->type < SDF_EVENT_ROUTER_TYPE_COUNT` before using it to index the subscriber table, for any event reaching dispatch regardless of entry point.

#### Scenario: Out-of-range or internal wake type is dropped, not dispatched
- **WHEN** an event with `event->type == SDF_EVENT_ROUTER_INTERNAL_WAKE` or `type >= SDF_EVENT_ROUTER_TYPE_COUNT` reaches dispatch
- **THEN** it is logged and dropped without indexing the subscriber table and without invoking any callback
