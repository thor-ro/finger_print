## Purpose

Split the monolithic `sdf_services_task` into focused FreeRTOS tasks communicating via the event router. Each task handles one responsibility with appropriate priority.

## ADDED Requirements

### Requirement: Every Long-Running Service Task Participates In The Task Watchdog
Every long-running `sdf_services` task SHALL be registered with the task watchdog for
the entire time it is running, and SHALL report liveness at least once per iteration of
its main loop, so that a task which stops making progress trips the watchdog and
reboots the device instead of failing silently. A task SHALL deregister from the
watchdog as part of its cooperative shutdown, before it deletes itself, so that an
intentionally stopped task is not reported as unresponsive.

This applies uniformly to the match, enrollment and admin tasks. The admin task is
called out explicitly because it is the one that has never participated: a wedged admin
task stops servicing button presses, admin action requests and admin action timeouts
with no reboot and no diagnostic, which is indistinguishable from a hardware button
fault.

#### Scenario: Admin task stops making progress

- **WHEN** the admin task stops returning to its main loop for longer than the
  configured watchdog timeout
- **THEN** the watchdog reports the admin task as unresponsive and the device reboots,
  rather than the device continuing to run with admin actions permanently unserviced

#### Scenario: Admin task is idle but healthy

- **WHEN** the admin task is running normally with no admin action pending and no events
  arriving
- **THEN** its bounded wait returns often enough that it reports liveness within the
  watchdog timeout, and the device does not reboot

#### Scenario: Service task is stopped on purpose

- **WHEN** a service task exits its main loop in response to a stop request and cleans
  up before deleting itself
- **THEN** it deregisters from the watchdog first, and its deliberate disappearance does
  not cause the watchdog to fire

#### Scenario: Watchdog participation is observable on the host test target

- **WHEN** the host test target starts the service tasks and then stops them
- **THEN** the tests can observe that each long-running service task became
  watchdog-registered while running and is no longer registered after stopping, so a
  future task that omits registration fails the suite rather than shipping unnoticed
