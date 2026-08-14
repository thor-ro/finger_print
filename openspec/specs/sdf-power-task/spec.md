# Spec: sdf_power Task Architecture

## Purpose

Defines `sdf_power_task`'s wait/wake behavior while it holds a stay-awake decision, so the task cooperates with FreeRTOS automatic light sleep instead of defeating it with a fixed fast poll.

## Requirements

### Requirement: Stay-Awake Wait Is Deadline-Computed
While `sdf_power_task` holds a `STAY_AWAKE` decision, it SHALL compute its next wait duration from the nearest pending deadline (remaining idle-before-sleep time, remaining post-wake-guard time, or remaining time to the next battery report) rather than waking at a fixed short interval regardless of how far away the next relevant deadline is.

#### Scenario: Nearest deadline is idle-before-sleep
- **WHEN** the task is in `STAY_AWAKE` and the idle-before-sleep timer has the least time remaining of the tracked deadlines
- **THEN** the task's computed wait targets that remaining time rather than a fixed short interval

#### Scenario: Nearest deadline is the post-wake guard
- **WHEN** the task is in `STAY_AWAKE` shortly after waking, within the post-wake guard window
- **THEN** the task's computed wait targets the remaining guard time

### Requirement: Wait Duration Is Capped For Watchdog Safety
The computed wait duration SHALL be capped at a value low enough that the task can reset its task-watchdog registration before the configured task-watchdog timeout elapses, even when the nearest deadline is farther away than that cap.

#### Scenario: Deadline farther away than the watchdog-safe cap
- **WHEN** the nearest pending deadline is farther away than the watchdog-safe cap
- **THEN** the task waits only up to the cap, resets the task watchdog, and re-evaluates its decision

#### Scenario: Deadline sooner than the watchdog-safe cap
- **WHEN** the nearest pending deadline is sooner than the watchdog-safe cap
- **THEN** the task waits until that deadline rather than the full cap

### Requirement: Fresh Activity Wakes The Task Early
`sdf_power_mark_activity()` SHALL cause `sdf_power_task` to re-evaluate its decision before its currently computed wait duration would otherwise elapse, so newly recorded activity is not discovered only after the task sleeps through a now-stale deadline.

#### Scenario: Activity recorded mid-wait
- **WHEN** `sdf_power_mark_activity()` is called while `sdf_power_task` is waiting on a previously computed deadline
- **THEN** the task wakes and recomputes its decision using the new activity timestamp, rather than waiting out the original duration

### Requirement: Deliberate Sleep Entry Is Unaffected
The deadline-computed wait and watchdog-safe cap SHALL only govern the `STAY_AWAKE` spell. Reaching a `SLEEP_LIGHT` or `SLEEP_DEEP` decision SHALL continue to enter sleep exactly as before this change.

#### Scenario: Sleep decision reached
- **WHEN** policy evaluation returns `SLEEP_LIGHT` or `SLEEP_DEEP`
- **THEN** the task enters sleep via the existing mechanism, unaffected by the stay-awake wait computation
