# sdf-app-task Specification

## ADDED Requirements

### Requirement: The application component owns a task

The application component SHALL own a FreeRTOS task on which its event handling runs. Its event-router subscriber callbacks SHALL NOT perform application work themselves.

This exists because the component has event-router subscriptions but no execution context of its own, so every handler runs on the router's single dispatch task — the only consumer of the router queue and the context on which every other subscriber's dispatch is serialized. Work performed there is work no other subscriber can proceed past, and an emit issued from there is a wait for queue space that only the waiting task can create.

The handlers SHALL keep their existing behaviour; only the task they run on changes.

#### Scenario: Application work does not run on the dispatch task

- **WHEN** an event of a type the application component subscribes to is dispatched
- **THEN** the application's handling of that event runs on the application component's own task
- **AND** the dispatch task returns from the subscriber callback without having performed that handling

#### Scenario: A long handler does not stall other subscribers

- **WHEN** the application component handles an event whose processing takes hundreds of milliseconds, such as a key-stretching operation followed by a persistent-storage write
- **THEN** the dispatch task continues delivering subsequent events to other subscribers while that handling is in progress

#### Scenario: A handler that emits does not wait on the dispatch task's own queue

- **WHEN** an application handler emits a follow-on event
- **THEN** the emit is issued from the application task, whose wait for router queue space can be satisfied by the dispatch task draining it

### Requirement: The subscriber callback is a non-blocking trampoline

Each of the application component's event-router subscriptions SHALL be served by a callback that copies the event into a queue owned by the application task and returns, performing no other work.

The copy SHALL be enqueued with a zero wait. The callback SHALL NOT block for any duration, on the queue or on anything else.

The zero wait is a correctness requirement, not a performance preference. The application task may wait, bounded, to emit onto the router queue, so the two queues form a cycle. A cycle in which every edge can block is a deadlock; this one is safe only because the dispatch task's edge into the application queue never waits. Any change giving that enqueue a non-zero wait reintroduces the possibility of both parties waiting on each other.

#### Scenario: Callback returns without waiting

- **WHEN** the dispatch task invokes an application subscriber callback
- **THEN** the callback enqueues a copy of the event and returns without waiting on the application queue

#### Scenario: Full application queue does not stall dispatch

- **WHEN** an application subscriber callback is invoked while the application queue is full
- **THEN** the callback returns immediately rather than waiting for space
- **AND** the dispatch task proceeds to the next subscriber and the next queued event

#### Scenario: The event is copied, not referenced

- **WHEN** an event is handed to an application subscriber callback
- **THEN** what the application task later processes is a copy, so it does not depend on storage owned by the dispatch task remaining valid

### Requirement: Critical priority survives the second queue hop

The router orders its own queue by event priority. The application queue SHALL preserve that ordering rather than degrading it to arrival order: an event of critical priority SHALL be placed ahead of already-queued events of lower priority, and events of other priorities SHALL be appended.

Without this, an event the router deliberately ranked above another can be processed after it, undoing the prioritisation at the component boundary. Security-relevant events including lockout entry are emitted at critical priority.

#### Scenario: Critical event overtakes queued lower-priority events

- **WHEN** a critical-priority event is enqueued to the application task while lower-priority events are already queued
- **THEN** the application task processes the critical event before those events

#### Scenario: Non-critical events keep arrival order

- **WHEN** several non-critical events are enqueued to the application task
- **THEN** the application task processes them in the order they were enqueued

### Requirement: Application queue drops are counted and logged

When an event cannot be enqueued to the application task because its queue is full, the system SHALL log the drop with the dropped event's type and SHALL maintain a count of drops.

A drop is a handler that never runs — a match that does not unlatch, a lockout that does not raise an alarm. Making it silent would trade a diagnosable stall for an undiagnosable omission. The count is what makes the queue depth reviewable against real burst behaviour rather than an assumption.

#### Scenario: Drop is observable

- **WHEN** an event is discarded because the application queue is full
- **THEN** a log entry identifying the dropped event's type is produced
- **AND** the drop count increases

#### Scenario: Drop count is available for inspection

- **WHEN** the application component's counters are inspected
- **THEN** the number of events dropped for queue-full is among them, distinguishable from other error counters

### Requirement: Subscriptions are registered before the application task is created

The application component SHALL register every event-router subscription it depends on before creating its task, and SHALL create its task before the event router is started. No subscription SHALL be registered from inside the task body.

This mirrors what is already required of the service tasks, for the same reason: a subscription registered concurrently with a dispatch, or after the subscriber table is frozen, is either a race or a failure.

#### Scenario: Subscriptions exist before the task runs

- **WHEN** the application task is created during initialization
- **THEN** all of its subscriptions are already registered, and its first loop iteration can receive any event of a subscribed type

#### Scenario: No event is missed between task creation and the first loop iteration

- **WHEN** an event of a subscribed type is dispatched after the application task is created but before its first loop iteration
- **THEN** the event is delivered to the application queue rather than lost

#### Scenario: Task creation precedes the subscriber table freeze

- **WHEN** initialization starts the event router and freezes the subscriber table
- **THEN** the application task already exists and its subscriptions are already registered

### Requirement: The application task participates in the task watchdog

The application task SHALL be registered with the task watchdog for as long as it is running, and SHALL report liveness at least once per iteration of its main loop.

The application task owns the lock-actuation path. If it stops making progress the device stops unlatching, with no reboot and no diagnostic — a failure indistinguishable from a hardware fault. It can stop making progress: its handlers include an unbounded wait on a lock held by another component.

The task watchdog watches only tasks that explicitly subscribe to it, so a task that omits registration is simply unwatched rather than reported.

The application component has no shutdown path — its task runs from initialization until reboot — so the deregistration-on-shutdown obligation that applies to the service tasks does not apply here. If a shutdown path is added later, it SHALL deregister before the task deletes itself.

#### Scenario: Application task stops making progress

- **WHEN** the application task does not return to its main loop for longer than the configured watchdog timeout
- **THEN** the watchdog reports it as unresponsive and the device reboots, rather than continuing to run with lock actions permanently unserviced

#### Scenario: Application task is idle but healthy

- **WHEN** the application task is running normally with no events arriving
- **THEN** its bounded wait returns often enough that it reports liveness within the watchdog timeout, and the device does not reboot

#### Scenario: Watchdog participation is observable on the host test target

- **WHEN** the host test target initializes the application component
- **THEN** the tests can observe that the application task became watchdog-registered, so a future change that drops registration fails the suite rather than shipping unnoticed

### Requirement: The idle application task uses a bounded blocking wait

With no events queued, the application task SHALL block waiting for one rather than run a fixed short poll, waking only as often as its watchdog registration requires.

Its wait cap SHALL NOT be shorter than the caps used by the service tasks, so that adding this task does not tighten the shortest periodic wake in the system and therefore does not shorten the automatic light-sleep window beyond what those tasks already impose.

#### Scenario: No events pending

- **WHEN** no event has arrived for the application task
- **THEN** it remains blocked, waking at most at its wait cap rather than on a fixed short poll interval

#### Scenario: Idle application task does not cap the light-sleep window further

- **WHEN** the system is idle
- **THEN** the application task does not wake on a sub-second fixed interval

#### Scenario: An arriving event wakes the task promptly

- **WHEN** an event is enqueued while the application task is blocked on its full wait cap
- **THEN** the task wakes and handles it without waiting out the remaining cap

### Requirement: The application task's stack is sized by measurement

The application task's stack size SHALL be derived from a measured high-water mark on its deepest handler path, not from an estimate. The measurement, the path it was taken on, and any respect in which it is a lower bound SHALL be recorded in the canonical task documentation alongside the resulting size.

The deepest path runs a 10,000-iteration key-stretching operation followed by persistent-storage access, and is not reachable by ordinary interaction under emulation, so a measurement requires deliberate stimulation of that path.

#### Scenario: Stack size traces to a measurement

- **WHEN** the application task's stack size is reviewed
- **THEN** the canonical task documentation gives the measured figure it was derived from and the path measured

#### Scenario: The measurement's limits are recorded

- **WHEN** the recorded measurement omits frames that could not be exercised in the measuring environment
- **THEN** it is recorded as a lower bound with the omission named, rather than as the worst case
