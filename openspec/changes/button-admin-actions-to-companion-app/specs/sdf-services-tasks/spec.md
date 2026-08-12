## ADDED Requirements

### Requirement: Admin-Only Actions Not Bound To Physical Button Gestures
The button task SHALL NOT bind any gesture to `SDF_SERVICES_ADMIN_ACTION_ENROLL_ADMIN` or `SDF_SERVICES_ADMIN_ACTION_ZB_JOIN`. These actions SHALL only be reachable via an authenticated BLE Companion Service request.

#### Scenario: Triple-click produces no action
- **WHEN** a triple-click occurs on the physical button
- **THEN** no admin action is triggered
- **AND** `pending_admin_action` state is unaffected

#### Scenario: Hold-3s produces no action
- **WHEN** the button is held for 3 seconds
- **THEN** no admin action is triggered
- **AND** `pending_admin_action` state is unaffected

### Requirement: Simplified Pre-Enrollment Bootstrap Branch
On an unclaimed device (zero enrolled users), the button task's immediate-execution bootstrap path SHALL treat only `SDF_SERVICES_ADMIN_ACTION_ENROLL` as eligible for unauthenticated immediate execution. `SDF_SERVICES_ADMIN_ACTION_ENROLL_ADMIN` SHALL NOT reach this path, since it is no longer bound to any button gesture.

#### Scenario: Unclaimed device, single-click still enrolls immediately
- **WHEN** a single-click occurs
- **AND** the device has zero enrolled users
- **THEN** the system starts enrollment immediately, without requiring admin authorization
