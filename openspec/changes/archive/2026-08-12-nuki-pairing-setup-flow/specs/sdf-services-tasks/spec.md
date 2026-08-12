## ADDED Requirements

### Requirement: State-Dependent Single-Click Setup Action
The `sdf_button_task` SHALL determine the action triggered by a single-click gesture dynamically at press time based on the device's current setup state, rather than from a fixed static gesture-to-action mapping. Setup state SHALL be derived from existing persisted state (enrolled user count, and whether `sdf_storage_nuki_load()` succeeds), not from a new dedicated flag.

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

### Requirement: Double-Press Gesture Retired
The button task SHALL NOT bind any admin action to the `BUTTON_DOUBLE_CLICK` gesture.

#### Scenario: Double-click produces no action
- **WHEN** a double-click occurs on the physical button
- **THEN** no admin action is triggered
- **AND** `pending_admin_action` state is unaffected

### Requirement: Nuki Pairing Unreachable By Button After Setup Complete
Once setup is complete (Nuki credentials persisted), no button gesture SHALL be capable of re-triggering `SDF_SERVICES_ADMIN_ACTION_NUKI_PAIR`. Re-pairing after setup completion SHALL only be reachable via a full factory reset (which clears persisted Nuki credentials, returning the device to the setup-incomplete state) or via an authenticated BLE Companion trigger.

#### Scenario: Single-click after setup complete does not re-trigger pairing
- **WHEN** setup is already complete
- **AND** a single-click occurs
- **THEN** the system triggers `SDF_SERVICES_ADMIN_ACTION_ENROLL`, not `NUKI_PAIR`

#### Scenario: Factory reset re-opens the Nuki pairing window
- **WHEN** a factory reset completes
- **THEN** persisted Nuki credentials are cleared
- **AND** the next single-click, after an admin is re-enrolled, triggers `SDF_SERVICES_ADMIN_ACTION_NUKI_PAIR` again
