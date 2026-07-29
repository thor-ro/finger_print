## MODIFIED Requirements

### Requirement: Power task uses consistent locking for config snapshot
The power task SHALL use a consistent locking pattern — acquire the lock once, copy all needed state into local variables, release the lock, then process all cycle work outside the lock.

#### Scenario: Single lock acquisition per power cycle
- **WHEN** `sdf_power_task()` begins a loop iteration
- **THEN** it acquires the lock once, copies `config`, `last_activity_us`, `wake_guard_until_us`, `next_battery_report_us`, and `battery_percent` into local variables, then releases the lock
- **AND** all subsequent processing (policy evaluation, battery callback, sleep decision) happens with the lock released
- **AND** only one additional lock acquisition occurs if battery state needs updating

#### Scenario: Battery state update uses a single lock acquisition
- **WHEN** the battery threshold is crossed and state needs updating
- **THEN** the power task acquires the lock, updates `battery_percent` and `next_battery_report_us` in a single transaction, then releases the lock

### Requirement: Battery events use a dedicated event type
Battery level updates SHALL be emitted as `SDF_EVENT_ROUTER_POWER_BATTERY` events rather than reusing `SDF_EVENT_ROUTER_POWER_SLEEP`.

#### Scenario: Battery update emits correct event type
- **WHEN** `sdf_power_push_battery_percent()` sends a battery event
- **THEN** the event type is `SDF_EVENT_ROUTER_POWER_BATTERY` (not `SDF_EVENT_ROUTER_POWER_SLEEP`)
- **AND** the event priority is `SDF_EVENT_ROUTER_PRIO_LOW`

#### Scenario: Power subscribers are not confused by battery events
- **WHEN** subscribers receive events of type `SDF_EVENT_ROUTER_POWER_SLEEP`
- **THEN** they are only triggered by actual sleep events, not battery updates