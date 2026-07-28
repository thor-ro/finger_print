# Proposal: Unified FreeRTOS Task Architecture

## Summary

Document and formalize the complete FreeRTOS task architecture for SDF v2.0, encompassing all 6 tasks with their priorities, communication patterns, stack allocations, and migration path from current monolithic implementation.

## Problem

Current task architecture is partially documented and inconsistent:
- `software-architecture.md` §6 lists 5 tasks that don't match reality
- `sdf_sas.md` §6 references `task_event_router` which doesn't exist
- Actual implementation has 3 explicit tasks + on-demand BLE
- `refactor-services-task` change will add 3 more tasks
- No unified view of priorities, stacks, core affinities, or communication contracts

## Solution

Create a canonical task architecture document that:
1. **Documents current state** (what exists now)
2. **Defines target state** (after `refactor-services-task` completes)
3. **Specifies contracts** between tasks (event types, queues, priorities)
4. **Guides future changes** (BLE task, OTA task, etc.)

## Target Task Architecture

```
┌─────────────────────────────────────────────────────────────────────────────────┐
│                        SDF FREERTOS TASK ARCHITECTURE                            │
│                           ESP32-C6 (Single Core, SMP=1)                         │
├─────────────┬──────────┬──────────┬──────────┬──────────┬──────────┬────────────┤
│ Task        │ Priority │ Stack    │ Core     │ Trigger  │ Comm     │ Owner      │
├─────────────┼──────────┼──────────┼──────────┼──────────┼──────────┼────────────┤
│ sdf_power   │ 4        │ 4 KB     │ 0        │ Timer    │ Events   │ sdf_power  │
│ sdf_zigbee  │ 5        │ 8 KB     │ 0        │ Event    │ Callbacks│ sdf_proto_ │
│             │          │          │          │ queue    │ + Events │ zb         │
├─────────────┼──────────┼──────────┼──────────┼──────────┼──────────┼────────────┤
│ sdf_match   │ 5 (HIGH) │ 6 KB     │ 0        │ 400ms    │ Events   │ sdf_svcs   │
│ sdf_enroll  │ 4 (NORM) │ 4 KB     │ 0        │ Event    │ Events   │ sdf_svcs   │
│ sdf_admin   │ 5 (HIGH) │ 4 KB     │ 0        │ Event    │ Events   │ sdf_svcs   │
│ sdf_button  │ 4 (NORM) │ 3 KB     │ 0        │ GPIO ISR │ Events   │ sdf_svcs   │
├─────────────┼──────────┼──────────┼──────────┼──────────┼──────────┼────────────┤
│ sdf_ota     │ 3 (LOW)  │ 8 KB     │ 0        │ Event    │ Events   │ sdf_ota    │
│ (future)    │          │          │          │          │          │            │
└─────────────┴──────────┴──────────┴──────────┴──────────┴──────────┴────────────┘

Total RAM (stacks): ~37 KB + overhead
```

## Task Specifications

### 1. sdf_power_task (Existing)
**File:** `firmware/components/sdf_power/src/sdf_power.c:178`
**Priority:** 4 (NORMAL)  
**Stack:** 4096 bytes
**Loop:** 250ms (configurable)
**Responsibilities:**
- Sleep/wake decisions based on activity, battery, wake guards
- Zigbee check-in timer coordination
- Battery percentage reporting (60s interval)
- BLE radio gating during sleep
- Deep sleep fallback when Zigbee not joined

**Events Consumed:** `SDF_EVT_POWER_MARK_ACTIVITY` (implicit via API)
**Events Emitted:** `SDF_EVT_POWER_SLEEP`, `SDF_EVT_POWER_WAKE`, `SDF_EVT_BATTERY_REPORT`
**API:** `sdf_power_mark_activity()`, `sdf_power_get_battery_percent()`

---

### 2. sdf_zigbee_task (Existing)
**File:** `firmware/components/sdf_protocol_zigbee/src/sdf_protocol_zigbee.c:941`
**Priority:** 5 (HIGH)  
**Stack:** 4096 bytes → **Target: 8192 bytes**
**Trigger:** ESP-Zigbee internal event queue
**Responsibilities:**
- Zigbee stack processing (commissioning, network steering)
- Attribute reporting (lock state, battery, alarm mask, user list)
- Incoming command dispatch (Lock/Unlock/Latch/Programming)
- OTA upgrade handling

**Events Consumed:** `SDF_EVT_ZIGBEE_PERMIT_JOIN`, `SDF_EVT_ZIGBEE_FACTORY_RESET`
**Events Emitted:** `SDF_EVT_ZIGBEE_COMMAND`, `SDF_EVT_ZIGBEE_STATE_CHANGE`, `SDF_EVT_ZIGBEE_OTA_STATUS`
**Callbacks:** `sdf_protocol_zigbee_set_command_handler()`

---

### 3. sdf_match_task (New - from refactor-services-task)
**Priority:** 5 (HIGH) - Must preempt enrollment for lock actions  
**Stack:** 6144 bytes  
**Loop:** 400ms (config: `CONFIG_SDF_SVC_MATCH_POLL_INTERVAL_MS`)
**Responsibilities:**
- Fingerprint sensor power management (on/off based on activity)
- 1:N match polling (`fp_match_1n()`)
- Match result → emit `SDF_EVT_BIOMETRIC_MATCH` or `SDF_EVT_BIOMETRIC_MATCH_FAILED`
- Security lockout tracking (5 failures/60s → 120s lockout)
- Match cooldown enforcement
- Boot-time sensor probe & user query

**Events Consumed:** `SDF_EVT_POWER_WAKE`, `SDF_EVT_POWER_SLEEP`
**Events Emitted:** `SDF_EVT_BIOMETRIC_MATCH`, `SDF_EVT_BIOMETRIC_MATCH_FAILED`, `SDF_EVT_SECURITY_LOCKOUT_ENTERED`, `SDF_EVT_SECURITY_LOCKOUT_CLEARED`
**API:** None (pure event-driven)

---

### 4. sdf_enroll_task (New - from refactor-services-task)
**Priority:** 4 (NORMAL)  
**Stack:** 4096 bytes  
**Trigger:** Event-driven (enrollment start + step results)
**Responsibilities:**
- Execute enrollment steps via `fp_enroll_step()`
- Retry logic for ACK_FAIL (steps 1-2, configurable max)
- LED feedback per step (green pulse → solid green)
- Drive `sdf_enrollment_sm` state machine
- Emit completion/failure events

**Events Consumed:** `SDF_EVT_ENROLLMENT_START`, `SDF_EVT_ENROLLMENT_STEP_RESULT`
**Events Emitted:** `SDF_EVT_ENROLLMENT_STEP_COMPLETE`, `SDF_EVT_ENROLLMENT_COMPLETE`, `SDF_EVT_ENROLLMENT_FAILED`
**API:** None (pure event-driven)

---

### 5. sdf_admin_task (New - from refactor-services-task)
**Priority:** 5 (HIGH) - Must preempt match for admin actions  
**Stack:** 4096 bytes  
**Trigger:** Event-driven (admin request + biometric match)
**Responsibilities:**
- Wait for admin fingerprint match (`permission == 3`)
- 10s timeout (config: `CONFIG_SDF_SVC_ADMIN_TIMEOUT_MS`)
- Execute claimed admin action (Nuki pair, Zigbee join, factory reset, perm change)
- LED feedback (admin auth green/red)

**Events Consumed:** `SDF_EVT_ADMIN_ACTION_REQUEST`, `SDF_EVT_BIOMETRIC_MATCH` (admin only)
**Events Emitted:** `SDF_EVT_ADMIN_ACTION_COMPLETE`, `SDF_EVT_ADMIN_AUTH_RESULT`
**API:** None (pure event-driven)

---

### 6. sdf_button_task (New - from refactor-services-task)
**Priority:** 4 (NORMAL)  
**Stack:** 3072 bytes  
**Trigger:** GPIO ISR + software timer (debounce)
**Responsibilities:**
- GPIO ISR for enrollment button (GPIO 14)
- Debounce (50ms config: `CONFIG_SDF_SVC_BUTTON_DEBOUNCE_MS`)
- Multi-press detection (single/double/triple/long)
- Map to admin actions: ENROLL, NUKI_PAIR, ZB_JOIN, FACTORY_RESET, PERM_CHANGE
- Emit `SDF_EVT_ADMIN_ACTION_REQUEST` with action type

**Events Consumed:** None (ISR-driven)
**Events Emitted:** `SDF_EVT_ADMIN_ACTION_REQUEST`, `SDF_EVT_BUTTON_PRESS`
**API:** None (pure event-driven)

---

### 7. sdf_ota_task (Future)
**Priority:** 3 (LOW)  
**Stack:** 8192 bytes  
**Trigger:** Event-driven (OTA trigger + chunk events)
**Responsibilities:**
- Zigbee OTA download handling
- BLE OTA download handling (future)
- Signature verification (Ed25519)
- Partition write/verify/commit
- Rollback management

**Events Consumed:** `SDF_EVT_OTA_TRIGGER`, `SDF_EVT_OTA_CHUNK`
**Events Emitted:** `SDF_EVT_OTA_PROGRESS`, `SDF_EVT_OTA_COMPLETE`, `SDF_EVT_OTA_FAILED`

---

## Event Router Contracts

### Priority Mapping
| Event Type | Priority | Rationale |
|------------|----------|-----------|
| `SDF_EVT_SECURITY_LOCKOUT_ENTERED` | CRITICAL | Immediate alarm |
| `SDF_EVT_BIOMETRIC_MATCH` (admin) | CRITICAL | Admin auth for sensitive actions |
| `SDF_EVT_BIOMETRIC_MATCH` (user) | HIGH | Lock action latency critical |
| `SDF_EVT_ZIGBEE_COMMAND` | HIGH | Remote unlock latency |
| `SDF_EVT_ADMIN_ACTION_REQUEST` | HIGH | User-initiated config |
| `SDF_EVT_ENROLLMENT_START` | NORMAL | User-initiated, non-urgent |
| `SDF_EVT_POWER_SLEEP/WAKE` | NORMAL | Power management |
| `SDF_EVT_BATTERY_REPORT` | LOW | Telemetry |
| `SDF_EVT_OTA_PROGRESS` | LOW | Background |

### Queue Depths
| Task | Queue Depth | Overflow Strategy |
|------|-------------|-------------------|
| sdf_match | 16 | Drop oldest (new match more relevant) |
| sdf_enroll | 8 | Block (enrollment is sequential) |
| sdf_admin | 8 | Drop (user will retry) |
| sdf_button | 4 | Drop (debounce handles burst) |
| sdf_zigbee | 32 | Block (ESP-ZB internal) |
| sdf_power | 8 | Drop (periodic) |

---

## Migration Path

### Phase 0: Current State (Before refactor-services-task)
```
sdf_power_task (4)  ──► Events/Callbacks
sdf_zigbee_task (5) ──► Callbacks
sdf_services_task (4) ──► Everything else (monolithic)
```

### Phase 1: Add Event Router (add-event-router)
```
sdf_power_task (4)
sdf_zigbee_task (5)
sdf_services_task (4) ──► Emits/consumes via event router
sdf_event_router (N/A - library)
```

### Phase 2: Split Services Task (refactor-services-task)
```
sdf_power_task (4)
sdf_zigbee_task (5)
sdf_match_task (5)
sdf_enroll_task (4)
sdf_admin_task (5)
sdf_button_task (4)
sdf_event_router
```

### Phase 3: Future Tasks
```
... + sdf_ota_task (3)
```

---

## Documentation Updates

### Files to Update
| File | Section | Change |
|------|---------|--------|
| `doc/sdf_sas.md` | §6 Runtime View | Replace task table with canonical architecture |
| `doc/software-architecture.md` | §6 Runtime Design | Align with actual implementation |
| `doc/rtos_tasks.md` | (new) | Detailed task specs, stacks, priorities |
| `AGENTS.md` | Component Structure | List all 6 tasks with owners |

### New File: `doc/rtos_tasks.md`
Canonical task reference with:
- Priority inheritance considerations
- Stack monitoring (`uxTaskGetStackHighWaterMark`)
- Watchdog assignments per task
- Core affinity (ESP32-C6 single core but SMP configs)
- Inter-task communication contracts

## Acceptance Criteria

- [ ] `doc/rtos_tasks.md` created with full task specifications
- [ ] `doc/sdf_sas.md` §6 updated to match
- [ ] `doc/software-architecture.md` §6 aligned
- [ ] `AGENTS.md` component list includes all 6 tasks
- [ ] Stack sizes validated with `uxTaskGetStackHighWaterMark()` on hardware
- [ ] Priority inversion analysis documented
- [ ] Watchdog timeout per task configured

## Risks

| Risk | Mitigation |
|------|------------|
| Stack overflow in sdf_match (sensor probe) | 6KB + monitor; move probe to init if needed |
| Priority inversion on event router | Event router lock-free; no mutex in hot path |
| Queue overflow under load | Depth tuned per task; sync emit for CRITICAL |
| Zigbee stack size too small | Increase to 8KB; monitor high water mark |