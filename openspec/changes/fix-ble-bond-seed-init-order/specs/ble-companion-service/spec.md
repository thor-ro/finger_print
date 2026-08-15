# Spec Delta: ble-companion-service

## MODIFIED Requirements

### Requirement: Shared NimBLE Lifecycle
The Companion Service SHALL register its GATT database with the existing NimBLE host before that host starts. `sdf_protocol_ble` SHALL be the sole owner of NimBLE initialization, host-task creation, and host lifecycle callbacks.

The Companion Service SHALL NOT call into the NimBLE host — including any read of the persisted bond store — before that host has been initialized. Registering a GATT database before host start is permitted and required; reading host-owned state before host start is not, because NimBLE store reads acquire the host lock and an uninitialized host lock is fatal rather than an error return.

Any Companion Service startup step that depends on host-owned state SHALL run after host initialization, and SHALL degrade to a logged, non-fatal outcome if it cannot complete, so that a failure in such a step leaves the device running rather than aborting application init.

#### Scenario: Companion and Nuki roles start together
- **WHEN** the application initializes BLE
- **THEN** the Companion Service registers before the shared NimBLE host starts
- **AND** the Nuki client and Companion Service operate as central and peripheral roles on that single host

#### Scenario: Bond store is only read once the host owns it
- **WHEN** the Companion Service seeds its allow list from NimBLE's persisted bond store
- **THEN** the read happens only after the shared NimBLE host has been initialized
- **AND** application init completes without aborting

#### Scenario: Bond store seeding fails
- **WHEN** seeding the allow list from the persisted bond store cannot run or returns an error
- **THEN** the device completes application init with an empty allow list and logs the failure
- **AND** the device remains reachable through the admin-gated pairing window rather than becoming unbootable

#### Scenario: Bonded companion reconnects after a reboot
- **WHEN** a companion device that was bonded before a reboot attempts to reconnect
- **THEN** the allow list has already been seeded with that bonded identity by the time the Companion Service accepts filtered connections
- **AND** the reconnect succeeds without re-pairing
