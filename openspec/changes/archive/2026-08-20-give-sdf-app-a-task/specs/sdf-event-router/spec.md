# SDF Event Router

## MODIFIED Requirements

### Requirement: Dispatch runs without acquiring a lock
Because the subscriber table is frozen before dispatch begins, dispatch SHALL iterate the subscribers for an event type directly, without acquiring a lock and without copying subscribers into an intermediate buffer. Dispatch SHALL NOT drop an event due to lock contention, and SHALL NOT impose a runtime cap on the number of subscribers invoked for one event. Dispatch SHALL run only on the router's dispatch task, so subscriber callbacks never execute on an emitting task's stack and are never nested inside one another.

A subscriber callback SHALL NOT emit. The router SHALL reject any emit issued while dispatch is in progress on the dispatch task, at every priority and for every send timeout, and SHALL log the rejection so the offending call site is identifiable. This is a hard rejection rather than a coerced non-blocking send, because an emit from dispatch that finds the queue full can never succeed: the dispatch task is the queue's only consumer, so no other context can free space while it waits. A subscriber that must produce a follow-on event SHALL hand the work to a task it owns and emit from there.

The rejection is scoped to the dispatch task. An emit issued from any other task SHALL be accepted even while a dispatch is concurrently in progress, because that caller is not the queue's consumer and its wait for space can be satisfied.

#### Scenario: Every matching subscriber is invoked
- **WHEN** an event is dispatched and multiple subscribers are registered for its type at an accepting `min_prio`
- **THEN** every one of those subscribers' callbacks is invoked, with no runtime fan-out limit truncating the set

#### Scenario: Reentrant critical emit from a callback is safe
- **WHEN** a subscriber callback calls `sdf_event_router_emit()` with a `PRIO_CRITICAL` event during dispatch
- **THEN** the call is rejected with `ESP_ERR_INVALID_STATE` and the event is neither enqueued nor dispatched
- **AND** the dispatch task does not block, so the outcome is the same whether the queue is empty or full
- **AND** the previous guarantee that such an event would be enqueued and later dispatched "without being dropped" no longer holds and was never achievable on a full queue, since the dispatch task is the queue's only consumer

#### Scenario: Emit from within a subscriber callback is rejected
- **WHEN** a subscriber callback calls `sdf_event_router_emit()` during dispatch, at any priority and with any send timeout including `0`
- **THEN** the call returns `ESP_ERR_INVALID_STATE` without enqueuing the event and without blocking the dispatch task
- **AND** the rejection is logged with the rejected event's type and priority

#### Scenario: Dispatch is not stalled by a rejected emit
- **WHEN** a subscriber callback attempts to emit while the router queue is full
- **THEN** the attempt returns immediately rather than waiting for queue space
- **AND** the dispatch task proceeds to the next subscriber and then to the next queued event, so a full queue drains rather than deepening

#### Scenario: Emit from a task the subscriber owns is permitted
- **WHEN** a subscriber callback forwards an event to a queue owned by its own task, and that task later calls `sdf_event_router_emit()` from its own context
- **THEN** the emit is accepted and follows the ordinary enqueue and send-timeout semantics

#### Scenario: The ban is scoped to the dispatch task
- **WHEN** any task other than the router's dispatch task calls `sdf_event_router_emit()` while a dispatch is concurrently in progress
- **THEN** the emit is accepted, because that caller is not the queue's consumer and its wait for queue space can be satisfied

#### Scenario: No subscriber relies on the rejected behaviour
- **WHEN** the firmware runs with the rejection in place
- **THEN** no rejection is logged in ordinary operation, because every subscriber that needs to produce a follow-on event hands off to a task it owns first
- **AND** a logged rejection therefore identifies a regression rather than a known limitation

#### Scenario: Callbacks do not run on an emitter's stack
- **WHEN** any task emits an event of any priority
- **THEN** no subscriber callback executes on that task's stack, so the emitting task does not inherit a callback's blocking behaviour or stack usage
