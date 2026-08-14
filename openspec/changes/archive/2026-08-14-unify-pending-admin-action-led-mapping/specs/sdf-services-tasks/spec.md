## ADDED Requirements

### Requirement: Pending Admin Action LED Indication Is Path-Independent
When an admin action becomes pending (awaiting admin-fingerprint authorization), the system SHALL produce the LED indication assigned to that action, and that indication SHALL be identical regardless of which request path caused the action to become pending (physical button gesture, event-router admin-action request, or direct authenticated request).

#### Scenario: Same action, different origin, same indication
- **WHEN** a given admin action becomes pending via one request path
- **AND** the same admin action later becomes pending via a different request path
- **THEN** the LED indication is the same in both cases

#### Scenario: BLE Companion pairing window becomes pending
- **WHEN** `SDF_SERVICES_ADMIN_ACTION_BLE_PAIRING_WINDOW` becomes the pending admin action
- **THEN** the system produces its assigned LED indication, on every path that can set it pending

#### Scenario: Action with no assigned indication
- **WHEN** an admin action that has no assigned LED indication becomes pending
- **THEN** the system produces no LED indication for it, and does not fall back to another action's indication

### Requirement: Pending Admin Action LED Mapping Is Complete
The system SHALL define an LED indication for every admin action that can be set pending, so that the device never enters an awaiting-admin-fingerprint state without user-visible feedback that it is waiting.

#### Scenario: Every settable pending action has feedback
- **WHEN** any admin action is set as `pending_admin_action`
- **THEN** the user receives an LED indication that the device is awaiting admin authorization
