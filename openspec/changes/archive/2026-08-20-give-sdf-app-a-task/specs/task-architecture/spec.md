# task-architecture Specification

## MODIFIED Requirements

### Requirement: Documentation updates
The system SHALL keep all affected documentation files matching the canonical task architecture. Where these documents state or enumerate the set of tasks, they SHALL reflect the set the firmware actually creates at the time of writing, rather than a count fixed at the time the document was first produced.

#### Scenario: sdf_sas.md §6 updated
- **WHEN** viewing `doc/sdf_sas.md` §6 Runtime View
- **THEN** the task table matches the canonical architecture in `doc/rtos_tasks.md`

#### Scenario: software-architecture.md §6 updated
- **WHEN** viewing `doc/software-architecture.md` §6 Runtime Design
- **THEN** it is aligned with the actual implementation, listing the tasks the firmware creates rather than a superseded count

#### Scenario: AGENTS.md component list updated
- **WHEN** viewing `AGENTS.md` Component Structure
- **THEN** every task the firmware creates is listed with its owning component

#### Scenario: New doc/rtos_tasks.md created
- **WHEN** viewing `doc/rtos_tasks.md`
- **THEN** the file exists with full task specifications

## ADDED Requirements

### Requirement: The canonical task table matches the tasks the firmware creates

`doc/rtos_tasks.md` is the single source of truth for task definitions, priorities and stack sizes. Every task the firmware creates SHALL have an entry in it, and every entry SHALL correspond to a task the firmware creates. Adding, removing or renaming a task SHALL update that table in the same change.

This is stated as an ongoing correspondence rather than a one-time documentation task because it has already drifted: the table was written against a six-task snapshot while the firmware creates more, and the tasks it omits include the event router's dispatch task — the one on which every subscriber's work is serialized, and therefore the one whose absence is most misleading to anyone reasoning about where work runs.

The table SHALL record, per task, at least its name, priority, stack size, and the component that owns it.

#### Scenario: A task exists that the table omits

- **WHEN** the firmware creates a task with no entry in `doc/rtos_tasks.md`
- **THEN** the table is incomplete and SHALL be corrected, regardless of whether that task was introduced by the change under review

#### Scenario: A new task is introduced

- **WHEN** a change adds a FreeRTOS task
- **THEN** that change also adds the task's entry, with its priority, stack size and owning component, and updates any aggregate figures the document states

#### Scenario: A stack size is derived from a measurement

- **WHEN** a task's stack size was set from a measured high-water mark
- **THEN** the table records the measurement and the path measured, so the size can be reviewed against evidence rather than re-derived from scratch

#### Scenario: Task set is inspected for review

- **WHEN** a reviewer compares the table against the task-creation call sites in the firmware
- **THEN** the two sets correspond, with no task present in one and absent from the other
