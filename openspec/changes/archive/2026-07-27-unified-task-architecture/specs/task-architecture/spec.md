## ADDED Requirements

### Requirement: Canonical task architecture table
The system SHALL define a canonical FreeRTOS task architecture table documenting all 6 tasks (7 including future OTA) with their priorities, stack sizes, core affinities, triggers, communication mechanisms, and owning components.

#### Scenario: Task architecture table exists
- **WHEN** developer views `doc/rtos_tasks.md`
- **THEN** a table lists all tasks with columns: Task, Priority, Stack, Core, Trigger, Comm, Owner

#### Scenario: Task architecture matches proposal
- **WHEN** comparing `doc/rtos_tasks.md` table to proposal.md table
- **THEN** all 6 tasks match: sdf_power (prio 4, 4KB), sdf_zigbee (prio 5, 8KB), sdf_match (prio 5, 6KB), sdf_enroll (prio 4, 4KB), sdf_admin (prio 5, 4KB), sdf_button (prio 4, 3KB)

#### Scenario: Future OTA task documented
- **WHEN** viewing `doc/rtos_tasks.md`
- **THEN** sdf_ota_task listed with priority 3, 8KB stack, event-driven trigger

### Requirement: Task detailed specifications
The system SHALL provide detailed specifications for each task including responsibilities, events consumed/emitted, APIs, configuration parameters, and timing.

#### Scenario: sdf_power_task specification complete
- **WHEN** viewing `doc/rtos_tasks.md` sdf_power section
- **THEN** responsibilities include: sleep/wake decisions, Zigbee check-in timer, battery reporting (60s), BLE radio gating, deep sleep fallback
- **THEN** events consumed: `SDF_EVT_POWER_MARK_ACTIVITY` (via API)
- **THEN** events emitted: `SDF_EVT_POWER_SLEEP`, `SDF_EVT_POWER_WAKE`, `SDF_EVT_BATTERY_REPORT`
- **THEN** API documented: `sdf_power_mark_activity()`, `sdf_power_get_battery_percent()`
- **THEN** loop interval: 250ms (configurable via `CONFIG_SDF_POWER_LOOP_INTERVAL_MS`)

#### Scenario: sdf_zigbee_task specification complete
- **WHEN** viewing `doc/rtos_tasks.md` sdf_zigbee section
- **THEN** responsibilities include: Zigbee stack processing, attribute reporting, incoming command dispatch, OTA handling
- **THEN** events consumed: `SDF_EVT_ZIGBEE_PERMIT_JOIN`, `SDF_EVT_ZIGBEE_FACTORY_RESET`
- **THEN** events emitted: `SDF_EVT_ZIGBEE_COMMAND`, `SDF_EVT_ZIGBEE_STATE_CHANGE`, `SDF_EVT_ZIGBEE_OTA_STATUS`
- **THEN** callback documented: `sdf_protocol_zigbee_set_command_handler()`
- **THEN** stack: 8KB (increased from 4KB)

#### Scenario: sdf_match_task specification complete
- **WHEN** viewing `doc/rtos_tasks.md` sdf_match section
- **THEN** responsibilities include: fingerprint sensor power mgmt, 1:N match polling, match result emission, security lockout tracking, match cooldown, boot-time probe
- **THEN** events consumed: `SDF_EVT_POWER_WAKE`, `SDF_EVT_POWER_SLEEP`
- **THEN** events emitted: `SDF_EVT_BIOMETRIC_MATCH`, `SDF_EVT_BIOMETRIC_MATCH_FAILED`, `SDF_EVT_SECURITY_LOCKOUT_ENTERED`, `SDF_EVT_SECURITY_LOCKOUT_CLEARED`
- **THEN** loop interval: 400ms (config: `CONFIG_SDF_SVC_MATCH_POLL_INTERVAL_MS`)
- **THEN** priority: 5 (HIGH) - must preempt enrollment

#### Scenario: sdf_enroll_task specification complete
- **WHEN** viewing `doc/rtos_tasks.md` sdf_enroll section
- **THEN** responsibilities include: execute enrollment steps via `fp_enroll_step()`, retry logic (ACK_FAIL steps 1-2), LED feedback, drive enrollment SM, emit completion/failure
- **THEN** events consumed: `SDF_EVT_ENROLLMENT_START`, `SDF_EVT_ENROLLMENT_STEP_RESULT`
- **THEN** events emitted: `SDF_EVT_ENROLLMENT_STEP_COMPLETE`, `SDF_EVT_ENROLLMENT_COMPLETE`, `SDF_EVT_ENROLLMENT_FAILED`
- **THEN** priority: 4 (NORMAL)
- **THEN** trigger: event-driven

#### Scenario: sdf_admin_task specification complete
- **WHEN** viewing `doc/rtos_tasks.md` sdf_admin section
- **THEN** responsibilities include: wait for admin fingerprint match (permission=3), 10s timeout, execute admin action (Nuki pair, Zigbee join, factory reset, perm change), LED feedback
- **THEN** events consumed: `SDF_EVT_ADMIN_ACTION_REQUEST`, `SDF_EVT_BIOMETRIC_MATCH` (admin only)
- **THEN** events emitted: `SDF_EVT_ADMIN_ACTION_COMPLETE`, `SDF_EVT_ADMIN_AUTH_RESULT`
- **THEN** timeout: 10s (config: `CONFIG_SDF_SVC_ADMIN_TIMEOUT_MS`)
- **THEN** priority: 5 (HIGH) - must preempt match

#### Scenario: sdf_button_task specification complete
- **WHEN** viewing `doc/rtos_tasks.md` sdf_button section
- **THEN** responsibilities include: GPIO ISR (GPIO 14), debounce (50ms), multi-press detection, map to 5 admin actions, emit admin action request
- **THEN** events consumed: none (ISR-driven)
- **THEN** events emitted: `SDF_EVT_ADMIN_ACTION_REQUEST`, `SDF_EVT_BUTTON_PRESS`
- **THEN** debounce: 50ms (config: `CONFIG_SDF_SVC_BUTTON_DEBOUNCE_MS`)
- **THEN** priority: 4 (NORMAL)

#### Scenario: sdf_ota_task specification complete
- **WHEN** viewing `doc/rtos_tasks.md` sdf_ota section
- **THEN** responsibilities include: Zigbee OTA download, BLE OTA download (future), signature verification (Ed25519), partition write/verify/commit, rollback management
- **THEN** events consumed: `SDF_EVT_OTA_TRIGGER`, `SDF_EVT_OTA_CHUNK`
- **THEN** events emitted: `SDF_EVT_OTA_PROGRESS`, `SDF_EVT_OTA_COMPLETE`, `SDF_EVT_OTA_FAILED`
- **THEN** priority: 3 (LOW)
- **THEN** stack: 8KB

### Requirement: Task ownership boundaries
The system SHALL assign each task to a single owning component with no cross-component task sharing.

#### Scenario: Task-component mapping documented
- **WHEN** viewing `doc/rtos_tasks.md`
- **THEN** sdf_power → sdf_power component
- **THEN** sdf_zigbee → sdf_protocol_zigbee component
- **THEN** sdf_match, sdf_enroll, sdf_admin, sdf_button → sdf_services component
- **THEN** sdf_ota (future) → sdf_ota component

## ADDED Requirements

### Requirement: Event router priority mapping
The system SHALL define event priority levels (CRITICAL, HIGH, NORMAL, LOW) mapped to FreeRTOS priorities with rationale for each event type.

#### Scenario: Priority mapping table complete
- **WHEN** viewing `doc/rtos_tasks.md` Priority Mapping section
- **THEN** CRITICAL: `SDF_EVT_SECURITY_LOCKOUT_ENTERED`, `SDF_EVT_BIOMETRIC_MATCH` (admin)
- **THEN** HIGH: `SDF_EVT_BIOMETRIC_MATCH` (user), `SDF_EVT_ZIGBEE_COMMAND`, `SDF_EVT_ADMIN_ACTION_REQUEST`
- **THEN** NORMAL: `SDF_EVT_ENROLLMENT_START`, `SDF_EVT_POWER_SLEEP/WAKE`
- **THEN** LOW: `SDF_EVT_BATTERY_REPORT`, `SDF_EVT_OTA_PROGRESS`

#### Scenario: Priority rationale documented
- **WHEN** viewing priority mapping
- **THEN** CRITICAL events justified as "Immediate alarm" or "Admin auth for sensitive actions"
- **THEN** HIGH events justified as "Lock action latency critical" or "Remote unlock latency" or "User-initiated config"
- **THEN** NORMAL events justified as "User-initiated, non-urgent" or "Power management"
- **THEN** LOW events justified as "Telemetry" or "Background"

### Requirement: Queue depths and overflow strategies
The system SHALL define queue depth and overflow strategy for each task's event queue.

#### Scenario: Queue depth table complete
- **WHEN** viewing `doc/rtos_tasks.md` Queue Depths section
- **THEN** sdf_match: depth 16, drop oldest
- **THEN** sdf_enroll: depth 8, block
- **THEN** sdf_admin: depth 8, drop
- **THEN** sdf_button: depth 4, drop
- **THEN** sdf_zigbee: depth 32, block
- **THEN** sdf_power: depth 8, drop

#### Scenario: Overflow strategy rationale documented
- **WHEN** viewing queue depths
- **THEN** sdf_match drop oldest justified as "new match more relevant"
- **THEN** sdf_enroll block justified as "enrollment is sequential"
- **THEN** sdf_admin drop justified as "user will retry"
- **THEN** sdf_button drop justified as "debounce handles burst"
- **THEN** sdf_zigbee block justified as "ESP-ZB internal"
- **THEN** sdf_power drop justified as "periodic"

## ADDED Requirements

### Requirement: Migration path documentation
The system SHALL document the 3-phase migration from current 3-task architecture to target 6-task architecture.

#### Scenario: Phase 0 current state documented
- **WHEN** viewing `doc/rtos_tasks.md` Migration Path
- **THEN** current: sdf_power_task (4), sdf_zigbee_task (5), sdf_services_task (4) monolithic

#### Scenario: Phase 1 event router documented
- **WHEN** viewing `doc/rtos_tasks.md` Migration Path
- **THEN** Phase 1: event router added, sdf_services_task emits/consumes via router

#### Scenario: Phase 2 split services documented
- **WHEN** viewing `doc/rtos_tasks.md` Migration Path
- **THEN** Phase 2: 6 tasks (power, zigbee, match, enroll, admin, button) + event router

#### Scenario: Phase 3 future tasks documented
- **WHEN** viewing `doc/rtos_tasks.md` Migration Path
- **THEN** Phase 3: + sdf_ota_task (3)

## ADDED Requirements

### Requirement: Documentation updates
The system SHALL update all affected documentation files to match the canonical task architecture.

#### Scenario: sdf_sas.md §6 updated
- **WHEN** viewing `doc/sdf_sas.md` §6 Runtime View
- **THEN** task table replaced with canonical architecture from proposal

#### Scenario: software-architecture.md §6 updated
- **WHEN** viewing `doc/software-architecture.md` §6 Runtime Design
- **THEN** aligned with actual implementation (6 tasks)

#### Scenario: AGENTS.md component list updated
- **WHEN** viewing `AGENTS.md` Component Structure
- **THEN** all 6 tasks listed with owning components

#### Scenario: New doc/rtos_tasks.md created
- **WHEN** viewing `doc/rtos_tasks.md`
- **THEN** file exists with full task specifications

### Requirement: Stack monitoring and watchdog assignments
The system SHALL document stack monitoring approach and watchdog timeout per task.

#### Scenario: Stack monitoring documented
- **WHEN** viewing `doc/rtos_tasks.md`
- **THEN** stack monitoring via `uxTaskGetStackHighWaterMark()` documented
- **THEN** validation on hardware required

#### Scenario: Watchdog assignments documented
- **WHEN** viewing `doc/rtos_tasks.md`
- **THEN** watchdog timeout per task configured
- **THEN** priority inversion analysis documented