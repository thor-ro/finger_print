## ADDED Requirements

### Requirement: Unauthenticated Bootstrap Bypass Is Restricted To Local Physical Origin
The zero-enrolled-users bootstrap bypass — executing an admin action without admin-fingerprint authorization because no admin exists to give it — SHALL be granted only to requests originating from a physical interaction with the device. A remotely-originated admin action request SHALL NOT be granted the bypass, regardless of how many users are enrolled.

#### Scenario: Local physical request on an unclaimed device
- **WHEN** an admin action is requested by physical interaction with the device
- **AND** the device has zero enrolled users
- **THEN** the action executes immediately without admin-fingerprint authorization

#### Scenario: Remote request on an unclaimed device
- **WHEN** an admin action is requested remotely
- **AND** the device has zero enrolled users
- **THEN** the request SHALL NOT execute without authorization
- **AND** it follows the ordinary admin-fingerprint pending-action flow, which cannot be satisfied while no admin exists

#### Scenario: Local physical request on a claimed device
- **WHEN** an admin action is requested by physical interaction with the device
- **AND** the device has at least one enrolled user
- **THEN** the bypass does not apply and the request follows the ordinary admin-fingerprint pending-action flow

### Requirement: Bootstrap Bypass Decision Is Single-Sited
The decision of whether a given admin action request may execute without admin-fingerprint authorization SHALL be made in one place, consulted by every path that can set or execute an admin action, rather than being reimplemented per request path.

#### Scenario: A new request path is introduced
- **WHEN** a new path for requesting an admin action is added
- **THEN** it obtains its authorization decision from the same single decision point as every existing path
- **AND** it cannot grant the bypass without passing an explicit request origin to that decision point

#### Scenario: Bypass is taken
- **WHEN** the single decision point grants the bootstrap bypass
- **THEN** any previously pending admin action state is cleared before the action executes, so the bypassed action does not leave a stale pending action behind

## MODIFIED Requirements

### Requirement: Simplified Pre-Enrollment Bootstrap Branch
On an unclaimed device (zero enrolled users), the admin-action authorization path's immediate-execution bootstrap branch SHALL route `SDF_SERVICES_ADMIN_ACTION_ENROLL` directly into local enrollment, and SHALL route every other button-reachable action into the configured admin-action execution callback. `SDF_SERVICES_ADMIN_ACTION_ENROLL_ADMIN` SHALL NOT reach this path at all, since it is no longer bound to any button gesture.

#### Scenario: Unclaimed device, single-click still enrolls immediately
- **WHEN** a single-click occurs
- **AND** the device has zero enrolled users
- **THEN** the system starts enrollment immediately, without requiring admin authorization

#### Scenario: Unclaimed device, a non-enroll button action executes immediately
- **WHEN** a button gesture bound to an admin action other than `SDF_SERVICES_ADMIN_ACTION_ENROLL` occurs
- **AND** the device has zero enrolled users
- **THEN** the action is executed immediately via the admin-action execution callback, without admin-fingerprint authorization
- **AND** no pending admin action is left set

#### Scenario: Admin-only action cannot reach the bootstrap branch
- **WHEN** the device has zero enrolled users
- **THEN** `SDF_SERVICES_ADMIN_ACTION_ENROLL_ADMIN` is not reachable by any button gesture and therefore never enters the bootstrap branch
