## ADDED Requirements

### Requirement: Button Handling Requires No Dedicated Task
Button press detection, classification, and dispatch SHALL be driven entirely by the button driver's own scan mechanism and its registered callbacks. The system SHALL NOT run a dedicated FreeRTOS task for button handling, and SHALL NOT consume a task stack, event queue, or event-router subscription for that purpose.

#### Scenario: No periodic button task wakeup
- **WHEN** the system is idle with no button press in progress
- **THEN** no button-related task wakes periodically, because no button task exists

#### Scenario: Press still detected and dispatched
- **WHEN** the button is physically pressed
- **THEN** the press is classified (single/double/long) and the corresponding action is dispatched, with the same resulting behavior as before the task was removed

#### Scenario: Button lifecycle follows service start/stop
- **WHEN** `sdf_services_start_tasks()` succeeds and later `sdf_services_stop_tasks()` is called
- **THEN** button handling is initialized on start and torn down on stop
- **AND** a subsequent start reinitializes button handling, so a stop/start cycle leaves the button functional

### Requirement: Enrollment Button Scan Quiesces When Idle
The enrollment button's GPIO scan SHALL stop running on a periodic timer once no press is in progress, and SHALL resume automatically on the next GPIO interrupt for that button, rather than scanning continuously regardless of press state.

#### Scenario: No press in progress
- **WHEN** the enrollment button has not been pressed and no debounce/press sequence is in progress
- **THEN** periodic scanning stops until the next physical press

#### Scenario: Press interrupts idle scanning
- **WHEN** the button is physically pressed while scanning is stopped
- **THEN** scanning resumes and the press is detected and classified normally (single/double/long-press)

### Requirement: Idle Service Task Loops Use Bounded Blocking Waits
When they have no work pending and no deadline sooner than their wait cap, `sdf_enroll_task` and `sdf_admin_task` SHALL block waiting for incoming events rather than run a fixed-interval poll, waking only as often as required to service any task watchdog registration they hold.

#### Scenario: No enrollment activity pending
- **WHEN** `sdf_enrollment_sm_is_active()` is false and no relevant event has arrived
- **THEN** `sdf_enroll_task` remains blocked, waking at most at its watchdog-safe cadence rather than on a fixed short poll interval

#### Scenario: No admin action pending
- **WHEN** no admin action is pending and no relevant event has arrived
- **THEN** `sdf_admin_task` remains blocked, waking at most at its wait cap rather than on a fixed short poll interval

#### Scenario: Idle tasks do not cap the automatic light-sleep window
- **WHEN** the system is idle with no enrollment active and no admin action pending
- **THEN** no service task in this capability wakes on a sub-second fixed interval

### Requirement: Pending Admin Action Timeout Is Deadline-Driven
`sdf_admin_task` SHALL detect expiry of the pending-admin-action timeout by waiting until that timeout's deadline, rather than by re-checking it on a fixed short interval. The timeout duration itself SHALL be unchanged; only the granularity with which expiry is noticed may loosen, bounded by the task's wait cap.

#### Scenario: An admin action is pending
- **WHEN** an admin action is pending
- **THEN** `sdf_admin_task`'s wait targets that action's expiry deadline, clamped to its wait cap

#### Scenario: Pending action expires
- **WHEN** the pending-admin-action timeout elapses
- **THEN** the action is cleared, the timeout indication is produced, and the action-complete notification is emitted, as before this change

#### Scenario: Pending action is authorized before expiry
- **WHEN** a pending action is authorized before its deadline
- **THEN** no timeout occurs and the task returns to its idle blocking wait

### Requirement: Setting A Pending Admin Action Wakes The Admin Task
Because a pending admin action can be set by callers that publish no event, setting `pending_admin_action` SHALL cause `sdf_admin_task` to re-evaluate its wait, so that the action's timeout countdown begins at the deadline it was actually set for rather than at the task's next scheduled wake.

#### Scenario: Pending action set by a non-publishing caller
- **WHEN** a pending admin action is set by a caller that emits no event
- **THEN** `sdf_admin_task` wakes and recomputes its wait against the new deadline

#### Scenario: Pending action set while the task is in a long idle wait
- **WHEN** a pending admin action is set while `sdf_admin_task` is blocked on its full wait cap
- **THEN** the task does not wait out the remaining cap before beginning to track the new deadline

### Requirement: Active Enrollment Retries On A State-Driven Cadence
While an enrollment is active, `sdf_enroll_task` SHALL retry the current step at a cadence driven by that step's own retry policy, rather than by an unconditional fixed-interval poll that also runs while idle.

#### Scenario: Step requires a retry
- **WHEN** the enrollment state machine reports a retryable step result
- **THEN** the next attempt for that step occurs on the step's own retry cadence, not on the idle-loop's poll interval

#### Scenario: Enrollment becomes idle again
- **WHEN** an enrollment completes or fails
- **THEN** `sdf_enroll_task` returns to the bounded blocking wait behavior of an idle task

### Requirement: Shutdown Signal Is Pushed, Not Polled
`sdf_services_stop_tasks()` SHALL deliver the stop request to every task whose loop wait was lengthened by this change — `sdf_enroll_task` and `sdf_admin_task` — in a way that wakes the task immediately, rather than requiring it to observe `stop_requested` only at its next periodic poll.

#### Scenario: Stop requested while the enroll task is idle and blocked
- **WHEN** `sdf_services_stop_tasks()` is called while `sdf_enroll_task` is blocked waiting for events
- **THEN** the task wakes immediately, unwinds, and clears its task handle without waiting for a periodic timeout to elapse

#### Scenario: Stop requested while the admin task is idle and blocked
- **WHEN** `sdf_services_stop_tasks()` is called while `sdf_admin_task` is blocked waiting for events
- **THEN** the task wakes immediately, unwinds, and clears its task handle without waiting for a periodic timeout to elapse

#### Scenario: Stop signal is lost
- **WHEN** the pushed stop signal cannot be delivered to a task
- **THEN** the task still observes `stop_requested` at its next wait-cap expiry, so shutdown completes within the existing overall stop budget

## MODIFIED Requirements

### Requirement: State-Dependent Single-Click Setup Action
The system SHALL determine the action triggered by a single-click gesture dynamically at press time based on the device's current setup state, rather than from a fixed static gesture-to-action mapping. Setup state SHALL be derived from existing persisted state (enrolled user count, and whether `sdf_storage_nuki_load()` succeeds), not from a new dedicated flag.

#### Scenario: Single-click on an unclaimed device
- **WHEN** a single-click occurs
- **AND** the device has zero enrolled users
- **THEN** the system triggers `SDF_SERVICES_ADMIN_ACTION_ENROLL`

#### Scenario: Single-click on a claimed device with setup incomplete
- **WHEN** a single-click occurs
- **AND** the device has at least one enrolled user
- **AND** `sdf_storage_nuki_load()` does not report previously persisted Nuki credentials
- **THEN** the system triggers `SDF_SERVICES_ADMIN_ACTION_NUKI_PAIR`
- **AND** the action follows the existing admin-fingerprint pending-action authorization flow, since an admin necessarily already exists in this state

#### Scenario: Single-click on a claimed device with setup complete
- **WHEN** a single-click occurs
- **AND** the device has at least one enrolled user
- **AND** `sdf_storage_nuki_load()` reports previously persisted Nuki credentials
- **THEN** the system triggers `SDF_SERVICES_ADMIN_ACTION_ENROLL`

### Requirement: Double-Press Requests BLE Companion Pairing Window
The system SHALL bind `BUTTON_DOUBLE_CLICK` to request the BLE Companion Service's admin-fingerprint-gated device pairing window, following the same `pending_admin_action` authorization flow used by every other admin action.

#### Scenario: Double-click requests the pairing window
- **WHEN** a double-click occurs on the physical button
- **AND** no other admin action is currently pending
- **THEN** the system sets `pending_admin_action` to request the BLE Companion pairing window
- **AND** awaits an Admin fingerprint scan within the pending-action timeout, per the existing admin-fingerprint pending-action pattern

#### Scenario: Double-click ignored while another admin action is pending
- **WHEN** a double-click occurs
- **AND** `pending_admin_action` is already set to a different action
- **THEN** the double-click SHALL NOT change the pending action

### Requirement: Admin-Only Actions Not Bound To Physical Button Gestures
The system SHALL NOT bind any gesture to `SDF_SERVICES_ADMIN_ACTION_ENROLL_ADMIN` or `SDF_SERVICES_ADMIN_ACTION_ZB_JOIN`. These actions SHALL only be reachable via an authenticated BLE Companion Service request.

#### Scenario: Triple-click produces no action
- **WHEN** a triple-click occurs on the physical button
- **THEN** no admin action is triggered
- **AND** `pending_admin_action` state is unaffected

#### Scenario: Hold-3s produces no action
- **WHEN** the button is held for 3 seconds
- **THEN** no admin action is triggered
- **AND** `pending_admin_action` state is unaffected

### Requirement: Simplified Pre-Enrollment Bootstrap Branch
On an unclaimed device (zero enrolled users), the button dispatch path's immediate-execution bootstrap branch SHALL treat only `SDF_SERVICES_ADMIN_ACTION_ENROLL` as eligible for unauthenticated immediate execution. `SDF_SERVICES_ADMIN_ACTION_ENROLL_ADMIN` SHALL NOT reach this path, since it is no longer bound to any button gesture.

#### Scenario: Unclaimed device, single-click still enrolls immediately
- **WHEN** a single-click occurs
- **AND** the device has zero enrolled users
- **THEN** the system starts enrollment immediately, without requiring admin authorization
