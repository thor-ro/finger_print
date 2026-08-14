## ADDED Requirements

### Requirement: Button Press Detection Performs Only Bounded Non-Blocking Work
The code invoked directly by the button driver's press-detection mechanism SHALL perform only bounded, non-blocking work. It SHALL NOT acquire a contended lock, SHALL NOT perform persistent-storage reads or writes, SHALL NOT perform peripheral I/O, and SHALL NOT execute an admin action.

#### Scenario: Single-click detected
- **WHEN** a single-click is detected
- **THEN** the press-detection path records the gesture and returns without reading persisted state to decide what the gesture means

#### Scenario: Long-press triggers a factory reset
- **WHEN** a long-press bound to a factory reset is detected on a device where that action executes immediately
- **THEN** the erase, fingerprint-template deletion, and reboot sequence does not run in the press-detection path
- **AND** the press-detection path returns promptly so other periodic driver work is not delayed

#### Scenario: Services lock is held by another task
- **WHEN** a button press is detected while another task holds the services lock
- **THEN** the press-detection path does not block waiting for that lock

### Requirement: Button Gestures Are Delivered As Events
A detected button gesture SHALL be published as an event describing the gesture, and SHALL be consumed by a task that performs the resolution, authorization, and execution associated with it. The published event SHALL identify the gesture, not a pre-resolved admin action.

#### Scenario: Gesture published and consumed
- **WHEN** a bound button gesture is detected
- **THEN** an event identifying that gesture is published
- **AND** a consuming task resolves it to an admin action and applies the existing authorization flow

#### Scenario: Resolution depends on persisted state
- **WHEN** the action a gesture maps to depends on persisted device state
- **THEN** that persisted state is read by the consuming task, not by the press-detection path

### Requirement: Button Press Publication Drops Under Backpressure
Publishing a button gesture event SHALL NOT block the press-detection path. If the event cannot be accepted for delivery immediately, the press SHALL be dropped and the drop recorded, rather than the publisher waiting for capacity.

#### Scenario: Event delivery capacity is exhausted
- **WHEN** a button gesture is detected and the event delivery mechanism cannot accept the event immediately
- **THEN** the press is dropped without the press-detection path waiting
- **AND** the drop is recorded

#### Scenario: Dropped press leaves no partial state
- **WHEN** a button press is dropped due to backpressure
- **THEN** no admin action is set pending, no LED indication is produced, and no action is executed
- **AND** a subsequent press is handled normally once capacity is available

## MODIFIED Requirements

### Requirement: State-Dependent Single-Click Setup Action
The system SHALL determine the action triggered by a single-click gesture dynamically based on the device's current setup state, rather than from a fixed static gesture-to-action mapping. This resolution SHALL occur when the gesture is consumed, not when it is detected, because it depends on persisted state. Setup state SHALL be derived from existing persisted state (enrolled user count, and whether `sdf_storage_nuki_load()` succeeds), not from a new dedicated flag.

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

#### Scenario: Setup state changes between detection and consumption
- **WHEN** a single-click is detected
- **AND** the device's setup state changes before the gesture is consumed
- **THEN** the action resolved is the one matching the setup state at consumption time

### Requirement: Double-Press Requests BLE Companion Pairing Window
The system SHALL bind a double-click gesture to request the BLE Companion Service's admin-fingerprint-gated device pairing window, following the same `pending_admin_action` authorization flow used by every other admin action.

#### Scenario: Double-click requests the pairing window
- **WHEN** a double-click occurs on the physical button
- **AND** no other admin action is currently pending
- **THEN** the system sets `pending_admin_action` to request the BLE Companion pairing window
- **AND** awaits an Admin fingerprint scan within the pending-action timeout, per the existing admin-fingerprint pending-action pattern

#### Scenario: Double-click ignored while another admin action is pending
- **WHEN** a double-click occurs
- **AND** `pending_admin_action` is already set to a different action
- **THEN** the double-click SHALL NOT change the pending action

#### Scenario: Pending state is evaluated at consumption time
- **WHEN** a double-click is detected while no action is pending
- **AND** another admin action becomes pending before the double-click is consumed
- **THEN** the double-click SHALL NOT displace that pending action
