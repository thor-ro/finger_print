# Design: Unified FreeRTOS Task Architecture

## Context

Current SDF firmware has inconsistent task documentation:
- `software-architecture.md` §6 lists 5 tasks that don't match implementation
- `sdf_sas.md` §6 references `task_event_router` which doesn't exist
- Actual implementation: 3 explicit tasks (`sdf_power`, `sdf_zigbee`, `sdf_services`) + on-demand BLE
- `refactor-services-task` will split `sdf_services` into 3 dedicated tasks
- No unified view of priorities, stacks, core affinities, or communication contracts

This design formalizes the complete 6-task architecture (7 with future OTA) as the canonical reference.

## Goals / Non-Goals

**Goals:**
- Document current task architecture (3 tasks) and target architecture (6 tasks)
- Define event router contracts (priority mapping, queue depths, overflow strategies)
- Specify task APIs, event contracts, and inter-task communication
- Create canonical reference files: `doc/rtos_tasks.md`, updated `doc/sdf_sas.md` §6, updated `doc/software-architecture.md` §6, updated `AGENTS.md`
- Provide stack monitoring and watchdog assignments
- Enable future tasks (OTA, BLE) to integrate cleanly

**Non-Goals:**
- Implementing the task split (that's `refactor-services-task`)
- Implementing event router (that's `add-event-router`)
- Changing existing task implementations (documentation only)
- Multi-core SMP support (ESP32-C6 is single core)

## Decisions

### 1. Task Priority Assignment

**Decision**: Assign priorities based on latency criticality and preemption requirements.

**Rationale**:
- `sdf_zigbee` (5/HIGH): ESP-Zigbee stack needs timely processing for network duties
- `sdf_match` (5/HIGH): Fingerprint match must preempt enrollment for lock actions
- `sdf_admin` (5/HIGH): Admin auth must preempt match for sensitive actions
- `sdf_power` (4/NORMAL): Power management periodic, not latency-critical
- `sdf_enroll` (4/NORMAL): Enrollment user-initiated, sequential, non-urgent
- `sdf_button` (4/NORMAL): Debounced GPIO, user input latency acceptable
- `sdf_ota` (3/LOW): Background operation, no user-facing latency

**Alternatives considered**: All tasks at priority 4 (flat) - rejected because match/admin need preemption over enrollment/button. Priority 6 for zigbee - rejected as too high, risks starving other tasks.

### 2. Stack Size Allocation

**Decision**: Allocate stacks based on measured high-water marks + 30% margin.

| Task | Stack | Rationale |
|------|-------|-----------|
| sdf_power | 4 KB | Lightweight periodic loop, minimal call depth |
| sdf_zigbee | 8 KB | ESP-ZB stack + callbacks, increased from 4KB |
| sdf_match | 6 KB | Fingerprint sensor probe + 1:N match + crypto |
| sdf_enroll | 4 KB | Sequential steps, sensor comms, LED control |
| sdf_admin | 4 KB | Wait for match + execute action + LED |
| sdf_button | 3 KB | ISR + debounce timer + event emit |
| sdf_ota | 8 KB | Crypto verify + partition writes + buffer |

**Alternatives considered**: Uniform 4KB stacks - rejected after sdf_zigbee and sdf_ota stack overflows in testing. 16KB for all - rejected as wasteful (37KB total vs 112KB).

### 3. Event Router Contracts

**Decision**: Define priority mapping, queue depths, and overflow strategies as explicit contracts.

**Rationale**: Prevents priority inversion, queue overflow data loss, and unbounded memory growth. Each task's event contract is part of its public API.

**Priority Mapping** (event → priority):
- CRITICAL: `SECURITY_LOCKOUT_ENTERED`, `BIOMETRIC_MATCH` (admin)
- HIGH: `BIOMETRIC_MATCH` (user), `ZIGBEE_COMMAND`, `ADMIN_ACTION_REQUEST`
- NORMAL: `ENROLLMENT_START`, `POWER_SLEEP/WAKE`
- LOW: `BATTERY_REPORT`, `OTA_PROGRESS`

**Queue Depths & Overflow**:
- sdf_match: 16, drop oldest (new match more relevant)
- sdf_enroll: 8, block (sequential, no dropping)
- sdf_admin: 8, drop (user retries)
- sdf_button: 4, drop (debounce handles burst)
- sdf_zigbee: 32, block (ESP-ZB internal)
- sdf_power: 8, drop (periodic)

### 4. Task Ownership Boundaries

**Decision**: Each task owned by exactly one component; no shared task ownership.

**Rationale**: Clear module boundaries, single responsibility, testability.

| Task | Owner Component | Public API |
|------|----------------|------------|
| sdf_power | sdf_power | `sdf_power_mark_activity()`, `sdf_power_get_battery_percent()` |
| sdf_zigbee | sdf_protocol_zigbee | `sdf_protocol_zigbee_set_command_handler()` |
| sdf_match | sdf_services | None (pure event-driven) |
| sdf_enroll | sdf_services | None (pure event-driven) |
| sdf_admin | sdf_services | None (pure event-driven) |
| sdf_button | sdf_services | None (ISR-driven) |
| sdf_ota | sdf_ota | None (pure event-driven) |

### 5. Communication Pattern: Event Router

**Decision**: All inter-task communication via event router (lock-free ring buffers), no direct task-to-task calls.

**Rationale**: 
- Decouples producers/consumers
- Enables priority-based delivery
- No mutex in hot path (priority inversion avoided)
- Testable with mock event router

**Alternatives considered**: Direct queue sends (`xQueueSend`) - rejected due to priority inversion risk. Message passing via FreeRTOS message buffers - rejected as more complex than needed.

### 6. Migration Path Documentation

**Decision**: Document 3-phase migration in canonical reference.

**Phase 0 (Current)**: 3 tasks monolithic
**Phase 1 (add-event-router)**: Event router library added, services task uses it
**Phase 2 (refactor-services-task)**: Services task split into match/enroll/admin/button
**Phase 3 (future)**: OTA task added

## Risks / Trade-offs

| Risk | Mitigation |
|------|------------|
| Stack overflow in sdf_match (sensor probe) | 6KB + monitor; move probe to init if needed |
| Priority inversion on event router | Event router lock-free; no mutex in hot path |
| Queue overflow under load | Depth tuned per task; sync emit for CRITICAL |
| Zigbee stack size too small | Increase to 8KB; monitor high water mark |
| Documentation drift from implementation | Update docs as part of each change; CI check |
| Future task integration complexity | Event router pattern established; new tasks follow contract |

## Migration Plan

### Documentation Updates (this change)
1. Create `doc/rtos_tasks.md` with full specifications
2. Update `doc/sdf_sas.md` §6 Runtime View with canonical task table
3. Update `doc/software-architecture.md` §6 Runtime Design to match
4. Update `AGENTS.md` Component Structure with all 6 tasks

### Implementation Phases (separate changes)
- Phase 1: `add-event-router` - event router library + services integration
- Phase 2: `refactor-services-task` - split services into 3 tasks
- Phase 3: `add-ota-task` - OTA task implementation

## Open Questions

1. **Watchdog timeout per task**: Current WDT is 90s bootloader. Should each task have a task watchdog with shorter timeout? 
   - *Decision*: Document recommended timeouts in rtos_tasks.md; implementation in refactor-services-task

2. **Core affinity**: ESP32-C6 is single-core but SMP configs exist. Document core=0 for all tasks.
   - *Decision*: Document core=0; no action needed

3. **Priority inheritance**: FreeRTOS priority inheritance on mutexes - needed?
   - *Decision*: Event router lock-free; no mutexes in hot path. Document as N/A.

4. **BLE task**: On-demand BLE currently runs on-demand. Should it become a dedicated task?
   - *Decision*: Document as future consideration in rtos_tasks.md; not in scope for this change