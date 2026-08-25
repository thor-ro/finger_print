## ADDED Requirements

### Requirement: User-Management Verbs Report A Named Outcome

`sdf_services`' user-management entry points SHALL report a named outcome for each verb rather than an `esp_err_t` whose values are shared across unrelated conditions. The outcome SHALL be decided where the condition is known, not decoded by the caller from an ambiguous code.

`ESP_ERR_INVALID_STATE` is currently returned by `sdf_services_delete_user()` and `sdf_services_change_user_permission()` for uninitialised services, an in-flight admin action, the last-admin guard, and an already-active enrolment. Each of those SHALL become separately reportable.

Callers SHALL NOT reconstruct the reason from their own context. A caller that today explains an ambiguous code by reasoning about what must have happened SHALL instead report what the services layer said.

#### Scenario: Last-admin refusal reported by name
- **WHEN** a user-management verb is refused because it would leave no enrolled admin
- **THEN** the reported outcome names that condition specifically

#### Scenario: Busy reported by name
- **WHEN** a user-management verb is refused because another admin action, permission change or enrolment is in flight
- **THEN** the reported outcome names that condition specifically
- **AND** it differs from the outcome reported for the last-admin refusal

#### Scenario: Occupied enrolment id reported by name
- **WHEN** an enrolment is requested for a user id that is already enrolled
- **THEN** the reported outcome names that condition specifically
- **AND** the check is performed in the services layer rather than by each caller

#### Scenario: Duplicate name reported by name
- **WHEN** a rename is refused because another enrolled user holds the target name
- **THEN** the reported outcome names that condition specifically

#### Scenario: Callers do not infer the reason
- **WHEN** a caller renders a refusal to a user
- **THEN** it renders the reported outcome
- **AND** it does not derive the reason from assumptions about its own preconditions

### Requirement: User Deletion Is An Admin-Fingerprint-Gated Action

Deleting an enrolled user on behalf of a remote request SHALL be authorized by a live admin fingerprint scan resolved through the pending-admin-action gate, in the same way as Enroll-Admin, Nuki re-pairing, Zigbee join and Web Registration Authorization.

The last-admin guard SHALL be evaluated before the gate is armed, so that a deletion that can never be permitted does not ask anyone to scan a finger.

The action SHALL resolve on denial and on timeout as well as on authorization, per "Pending BLE-Originated Admin Actions Always Resolve", and SHALL have an LED mapping, per "Pending Admin Action LED Mapping Is Complete".

#### Scenario: Deletion authorized by an admin scan
- **WHEN** a remote deletion request is made and an admin fingerprint scan authorizes it
- **THEN** the user is deleted

#### Scenario: Deletion denied by a non-admin scan
- **WHEN** the scan that resolves a pending deletion is not an enrolled admin
- **THEN** no user is deleted
- **AND** the request resolves with a denial

#### Scenario: Deletion times out
- **WHEN** no scan resolves a pending deletion before the pending-action window expires
- **THEN** no user is deleted
- **AND** the request resolves with a timeout

#### Scenario: Impossible deletion is refused before anyone is asked to scan
- **WHEN** a remote deletion request targets the only enrolled admin
- **THEN** the request is refused by the last-admin guard
- **AND** no pending admin action is armed
- **AND** no LED indication for a pending action is raised

### Requirement: Remote Enrolment Cannot Bypass The Admin Fingerprint Gate

An enrolment requested over a remote transport SHALL arm the admin-fingerprint gate and SHALL start the enrolment state machine only once an admin fingerprint scan has authorized it. An authenticated session SHALL NOT be sufficient on its own, at any permission level.

The existing local entry points that arm an enrolment directly SHALL remain available to the physical button path and to the setup phase, which have their own authorization: an admin scan already claimed by the button gesture, and the time-bounded setup phase on a device with no enrolled users.

#### Scenario: Remote enrolment waits for a scan
- **WHEN** an enrolment is requested over a remote transport
- **THEN** the enrolment state machine is not started
- **AND** it starts only after an admin fingerprint scan authorizes the request

#### Scenario: Remote enrolment of an admin is equally gated
- **WHEN** the requested enrolment carries admin permission
- **THEN** it is subject to the same authorizing scan as any other permission level

#### Scenario: Button path unchanged
- **WHEN** the physical button gesture resolves to an enrolment after its own admin scan
- **THEN** the enrolment starts without a second authorizing scan

#### Scenario: Setup-phase first enrolment unchanged
- **WHEN** the setup phase enrols the first user on a device with no enrolled users
- **THEN** the enrolment starts without an authorizing scan, since no admin exists to perform one
