## ADDED Requirements

### Requirement: Service task subscriptions are registered before task creation
The match, enroll, and admin tasks SHALL NOT register their event-router subscriptions from inside their own task bodies. Service initialization SHALL register every subscription these tasks depend on before the corresponding task is created, so that no subscription can be registered concurrently with an event dispatch.

#### Scenario: Subscriptions exist before the task runs
- **WHEN** service initialization creates the match, enroll, or admin task
- **THEN** that task's subscriptions are already registered, and the task's first loop iteration can receive any event of a subscribed type

#### Scenario: No event is missed during task startup
- **WHEN** an event of a subscribed type is emitted between service initialization and the task's first loop iteration
- **THEN** the event is delivered to the task's queue rather than being lost because the subscription had not been registered yet

### Requirement: Service tasks do not deregister subscriptions on shutdown
Cooperative task shutdown SHALL NOT deregister event-router subscriptions. A task that exits its loop on `stop_requested` SHALL leave its subscriptions in place and SHALL ensure its callback tolerates being invoked after the task has exited, by discarding events when its queue is no longer serviceable.

#### Scenario: Shutdown leaves subscriptions registered
- **WHEN** a service task observes `stop_requested` and unwinds
- **THEN** it performs no subscription teardown before deleting itself

#### Scenario: Callback after task exit is harmless
- **WHEN** an event of a subscribed type is dispatched after the owning task has exited
- **THEN** the subscriber callback discards the event without dereferencing a torn-down queue and without crashing
