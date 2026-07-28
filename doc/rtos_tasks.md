# FreeRTOS Task Architecture — SDF v2.0

**Target:** ESP32-C6 (Single Core, SMP=1)  
**RTOS:** FreeRTOS (ESP-IDF v5.5.3)  
**Canonical Reference:** This document is the single source of truth for all task definitions, priorities, stacks, and communication contracts.

---

## 1. Task Architecture Overview

| Task | Priority | Stack | Core | Trigger | Comm | Owner |
|------|----------|-------|------|---------|------|-------|
| sdf_power | 4 (NORMAL) | 4 KB | 0 | Timer (250ms) | Events | sdf_power |
| sdf_zigbee | 5 (HIGH) | 8 KB | 0 | ESP-ZB queue | Callbacks + Events | sdf_protocol_zigbee |
| sdf_match | 5 (HIGH) | 6 KB | 0 | Timer (400ms) | Events | sdf_services |
| sdf_enroll | 4 (NORMAL) | 4 KB | 0 | Event-driven | Events | sdf_services |
| sdf_admin | 5 (HIGH) | 4 KB | 0 | Event-driven | Events | sdf_services |
| sdf_button | 4 (NORMAL) | 3 KB | 0 | GPIO ISR | Events | sdf_services |
| sdf_ota (future) | 3 (LOW) | 8 KB | 0 | Event-driven | Events | sdf_ota |

**Total Stack RAM:** ~37 KB + overhead

**Priority Levels (FreeRTOS 0-6):**
- CRITICAL = 6 (reserved for future immediate-response)
- HIGH = 5 (lock actions, Zigbee commands, admin auth)
- NORMAL = 4 (power mgmt, enrollment, button)
- LOW = 3 (OTA, telemetry)

---

## 2. Task Detailed Specifications

### 2.1 sdf_power_task

**File:** `firmware/components/sdf_power/src/sdf_power.c:178`  
**Entry:** `sdf_power_task()`  
**Priority:** 4 (NORMAL)  
**Stack:** 4096 bytes  
**Core:** 0  
**Loop Interval:** 250ms (config: `CONFIG_SDF_POWER_LOOP_INTERVAL_MS`)

**Responsibilities:**
- Sleep/wake decisions based on activity, battery, wake guards
- Zigbee check-in timer coordination (default 15s)
- Battery percentage reporting (60s interval)
- BLE radio gating during sleep
- Deep sleep fallback when Zigbee not joined
- Light sleep enable/disable (config: `CONFIG_SDF_POWER_ENABLE_LIGHT_SLEEP`)

**Events Consumed:**
- `SDF_EVT_POWER_MARK_ACTIVITY` (implicit via `sdf_power_mark_activity()` API)

**Events Emitted:**
- `SDF_EVT_POWER_SLEEP` — entering sleep
- `SDF_EVT_POWER_WAKE` — wake from sleep
- `SDF_EVT_BATTERY_REPORT` — periodic battery % (60s)

**Public API:**
```c
void sdf_power_mark_activity(void);
int sdf_power_get_battery_percent(void);
```

**Wake Sources:**
- GPIO 3 (fingerprint WAKE pin interrupt)
- Zigbee check-in timer (esp_timer)

**Sleep Behavior:**
- Default: Deep sleep
- BLE radio disabled when idle
- Fingerprint sensor powered off between match cycles
- LED off during sleep

**Configuration:**
| Parameter | Default | Config Key |
|-----------|---------|------------|
| Zigbee check-in interval | 15s | `CONFIG_SDF_POWER_CHECKIN_INTERVAL_MS` |
| Idle before sleep | 5s | `CONFIG_SDF_POWER_IDLE_BEFORE_SLEEP_MS` |
| Post-wake guard | 1.5s | `CONFIG_SDF_POWER_POST_WAKE_GUARD_MS` |
| Loop interval | 250ms | `CONFIG_SDF_POWER_LOOP_INTERVAL_MS` |
| Battery report interval | 60s | `CONFIG_SDF_POWER_BATTERY_REPORT_INTERVAL_MS` |
| Light sleep enable | enabled | `CONFIG_SDF_POWER_ENABLE_LIGHT_SLEEP` |
| BLE radio gating | enabled | `CONFIG_SDF_POWER_ENABLE_BLE_RADIO_GATING` |

**Watchdog:** Task watchdog recommended (timeout: 5s > 2× loop interval)

---

### 2.2 sdf_zigbee_task

**File:** `firmware/components/sdf_protocol_zigbee/src/sdf_protocol_zigbee.c:941`  
**Entry:** `sdf_zigbee_task()` (ESP-Zigbee internal)  
**Priority:** 5 (HIGH)  
**Stack:** 8192 bytes (increased from 4096)  
**Core:** 0  
**Trigger:** ESP-Zigbee internal event queue

**Responsibilities:**
- Zigbee stack processing (commissioning, network steering, rejoin)
- Attribute reporting (Lock State, Battery %, Alarm Mask, User List)
- Incoming command dispatch (Lock/Unlock/Latch/Programming)
- OTA upgrade handling (Zigbee OTA cluster)
- Network state management

**Events Consumed:**
- `SDF_EVT_ZIGBEE_PERMIT_JOIN` — open network for joining
- `SDF_EVT_ZIGBEE_FACTORY_RESET` — leave network, clear NVRAM

**Events Emitted:**
- `SDF_EVT_ZIGBEE_COMMAND` — Lock/Unlock/Latch/Programming received
- `SDF_EVT_ZIGBEE_STATE_CHANGE` — joined/left/rejoined
- `SDF_EVT_ZIGBEE_OTA_STATUS` — OTA progress/complete/failed

**Callbacks (set by sdf_app):**
```c
void sdf_protocol_zigbee_set_command_handler(sdf_zigbee_command_handler_t handler);
```

**Configuration:**
| Parameter | Default | Config Key |
|-----------|---------|------------|
| Endpoint | 1 | `CONFIG_SDF_ZIGBEE_ENDPOINT` |
| Check-in interval | 15s | `CONFIG_SDF_POWER_CHECKIN_INTERVAL_MS` |
| Stack size | 8192 | `CONFIG_SDF_ZIGBEE_TASK_STACK` |

**Watchdog:** ESP-ZB internal WDT; task watchdog timeout: 10s

---

### 2.3 sdf_match_task

**Entry:** `sdf_match_task()` (new — from `refactor-services-task`)  
**Priority:** 5 (HIGH) — must preempt enrollment for lock actions  
**Stack:** 6144 bytes  
**Core:** 0  
**Loop Interval:** 400ms (config: `CONFIG_SDF_SVC_MATCH_POLL_INTERVAL_MS`)

**Responsibilities:**
- Fingerprint sensor power management (on/off based on activity)
- 1:N match polling via `fp_match_1n()`
- Match result → emit `SDF_EVT_BIOMETRIC_MATCH` or `SDF_EVT_BIOMETRIC_MATCH_FAILED`
- Security lockout tracking (5 failures/60s → 120s lockout)
- Match cooldown enforcement
- Boot-time sensor probe & user query

**Events Consumed:**
- `SDF_EVT_POWER_WAKE` — enable sensor, start polling
- `SDF_EVT_POWER_SLEEP` — power off sensor, stop polling

**Events Emitted:**
- `SDF_EVT_BIOMETRIC_MATCH` — user_id, permission (priority: HIGH for user, CRITICAL for admin)
- `SDF_EVT_BIOMETRIC_MATCH_FAILED` — failure reason
- `SDF_EVT_SECURITY_LOCKOUT_ENTERED` — lockout active
- `SDF_EVT_SECURITY_LOCKOUT_CLEARED` — lockout expired

**Public API:** None (pure event-driven)

**Match Logic:**
1. On `POWER_WAKE`: power on sensor, wait for ready
2. Loop every 400ms: call `fp_match_1n()`
3. On match: check permission; if admin (3) and admin action pending → emit match (CRITICAL); else emit match (HIGH)
4. On failure: increment counter; if ≥5 in 60s → emit `SECURITY_LOCKOUT_ENTERED`, start 120s timer
5. On `POWER_SLEEP`: power off sensor, clear state

**Configuration:**
| Parameter | Default | Config Key |
|-----------|---------|------------|
| Poll interval | 400ms | `CONFIG_SDF_SVC_MATCH_POLL_INTERVAL_MS` |
| Fail threshold | 5 attempts | `CONFIG_SDF_SVC_SECURITY_FAIL_THRESHOLD` |
| Fail window | 60s | `CONFIG_SDF_SVC_SECURITY_FAIL_WINDOW_MS` |
| Lockout duration | 120s | `CONFIG_SDF_SVC_SECURITY_LOCKOUT_MS` |
| Stack size | 6144 | `CONFIG_SDF_SVC_MATCH_TASK_STACK` |

**Watchdog:** Task watchdog timeout: 2s (5× poll interval)

---

### 2.4 sdf_enroll_task

**Entry:** `sdf_enroll_task()` (new — from `refactor-services-task`)  
**Priority:** 4 (NORMAL)  
**Stack:** 4096 bytes  
**Core:** 0  
**Trigger:** Event-driven (enrollment start + step results)

**Responsibilities:**
- Execute enrollment steps via `fp_enroll_step()`
- Retry logic for ACK_FAIL (steps 1-2, configurable max)
- LED feedback per step (green pulse → solid green)
- Drive `sdf_enrollment_sm` state machine
- Emit completion/failure events

**Events Consumed:**
- `SDF_EVT_ENROLLMENT_START` — user_id, permission
- `SDF_EVT_ENROLLMENT_STEP_RESULT` — step result from sensor

**Events Emitted:**
- `SDF_EVT_ENROLLMENT_STEP_COMPLETE` — step completed
- `SDF_EVT_ENROLLMENT_COMPLETE` — user_id, success
- `SDF_EVT_ENROLLMENT_FAILED` — user_id, failure reason

**Public API:** None (pure event-driven)

**Enrollment Flow:**
1. Receive `ENROLLMENT_START(user_id, perm)` → `sm_start()`
2. Loop: `sm_apply_step_result(driver_result)` → get `next_action`
3. `EXECUTE_STEP`: power on sensor, `fp_enroll_step(cmd, user_id, perm)`
4. `RETRY_STEP`: log retry, LED pulse, re-run same step
5. `COMPLETE`: LED solid green, emit `ENROLLMENT_COMPLETE`, reset SM
6. `FAIL`: LED red, emit `ENROLLMENT_FAILED`, reset SM

**Retry Policy (configurable via `sdf_enrollment_retry_policy_t`):**
| Step | Max Retries | Default |
|------|-------------|---------|
| 1 (capture 1) | 3 | `CONFIG_SDF_SVC_ENROLL_MAX_RETRY_STEP1` |
| 2 (capture 2) | 3 | `CONFIG_SDF_SVC_ENROLL_MAX_RETRY_STEP2` |
| 3 (capture 3) | 0 | `CONFIG_SDF_SVC_ENROLL_MAX_RETRY_STEP3` |

**Watchdog:** Task watchdog timeout: 30s (enrollment can take ~10-15s)

---

### 2.5 sdf_admin_task

**Entry:** `sdf_admin_task()` (new — from `refactor-services-task`)  
**Priority:** 5 (HIGH) — must preempt match for admin actions  
**Stack:** 4096 bytes  
**Core:** 0  
**Trigger:** Event-driven (admin request + biometric match)

**Responsibilities:**
- Wait for admin fingerprint match (`permission == 3`)
- 10s timeout (config: `CONFIG_SDF_SVC_ADMIN_TIMEOUT_MS`)
- Execute claimed admin action:
  - Nuki pairing
  - Zigbee join/permit join
  - Factory reset
  - Permission change
- LED feedback (admin auth green/red)

**Events Consumed:**
- `SDF_EVT_ADMIN_ACTION_REQUEST` — action type (ENROLL, NUKI_PAIR, ZB_JOIN, FACTORY_RESET, PERM_CHANGE)
- `SDF_EVT_BIOMETRIC_MATCH` (admin only, permission=3)

**Events Emitted:**
- `SDF_EVT_ADMIN_ACTION_COMPLETE` — action, result
- `SDF_EVT_ADMIN_AUTH_RESULT` — success/failure, action

**Public API:** None (pure event-driven)

**Admin Flow:**
1. Button press → `BUTTON` emits `ADMIN_ACTION_REQUEST(action)`
2. Admin task sets `pending_action = action`, starts 10s timer
3. Wait for `BIOMETRIC_MATCH` with `permission == 3`
4. On match: claim action, execute, emit `ADMIN_ACTION_COMPLETE`
5. On non-admin match: flash red, continue waiting
6. On timeout: emit `ADMIN_AUTH_RESULT` failure, clear pending

**Configuration:**
| Parameter | Default | Config Key |
|-----------|---------|------------|
| Auth timeout | 10s | `CONFIG_SDF_SVC_ADMIN_TIMEOUT_MS` |
| Stack size | 4096 | `CONFIG_SDF_SVC_ADMIN_TASK_STACK` |

**Watchdog:** Task watchdog timeout: 15s (> auth timeout)

---

### 2.6 sdf_button_task

**Entry:** `sdf_button_task()` (new — from `refactor-services-task`)  
**Priority:** 4 (NORMAL)  
**Stack:** 3072 bytes  
**Core:** 0  
**Trigger:** GPIO ISR (GPIO 14) + software timer (debounce)

**Responsibilities:**
- GPIO ISR for enrollment button (GPIO 14, active low)
- Debounce (50ms config: `CONFIG_SDF_SVC_BUTTON_DEBOUNCE_MS`)
- Multi-press detection (single/double/triple/long)
- Map to admin actions: ENROLL, NUKI_PAIR, ZB_JOIN, FACTORY_RESET, PERM_CHANGE
- Emit `SDF_EVT_ADMIN_ACTION_REQUEST` with action type

**Events Consumed:** None (ISR-driven)

**Events Emitted:**
- `SDF_EVT_ADMIN_ACTION_REQUEST` — action type
- `SDF_EVT_BUTTON_PRESS` — press type (single/double/triple/long)

**Press Mapping:**
| Press | Action | Description |
|-------|--------|-------------|
| Single | ENROLL | Start enrollment |
| Double | NUKI_PAIR | Initiate Nuki pairing |
| Triple | ZB_JOIN | Permit Zigbee join |
| Long (≥1s) | FACTORY_RESET | Factory reset device |
| Quad | PERM_CHANGE | Change user permission |

**Configuration:**
| Parameter | Default | Config Key |
|-----------|---------|------------|
| Debounce | 50ms | `CONFIG_SDF_SVC_BUTTON_DEBOUNCE_MS` |
| Long press threshold | 1000ms | `CONFIG_SDF_SVC_BUTTON_LONG_PRESS_MS` |
| Multi-press window | 500ms | `CONFIG_SDF_SVC_BUTTON_MULTI_WINDOW_MS` |
| Stack size | 3072 | `CONFIG_SDF_SVC_BUTTON_TASK_STACK` |

**Watchdog:** Task watchdog timeout: 5s

---

### 2.7 sdf_ota_task (Future)

**Entry:** `sdf_ota_task()` (planned — `add-ota-task`)  
**Priority:** 3 (LOW)  
**Stack:** 8192 bytes  
**Core:** 0  
**Trigger:** Event-driven (OTA trigger + chunk events)

**Responsibilities:**
- Zigbee OTA download handling
- BLE OTA download handling (future)
- Signature verification (Ed25519)
- Partition write/verify/commit
- Rollback management

**Events Consumed:**
- `SDF_EVT_OTA_TRIGGER` — source (zigbee/ble), version
- `SDF_EVT_OTA_CHUNK` — data chunk, offset, total

**Events Emitted:**
- `SDF_EVT_OTA_PROGRESS` — percent, bytes written
- `SDF_EVT_OTA_COMPLETE` — success, new version
- `SDF_EVT_OTA_FAILED` — error code

**Configuration:**
| Parameter | Default | Config Key |
|-----------|---------|------------|
| Stack size | 8192 | `CONFIG_SDF_OTA_TASK_STACK` |
| Chunk size | 512 bytes | `CONFIG_SDF_OTA_CHUNK_SIZE` |
| Verify on write | enabled | `CONFIG_SDF_OTA_VERIFY_WRITE` |

**Watchdog:** Task watchdog timeout: 60s (OTA can take 30-60s)

---

## 3. Event Router Contracts

### 3.1 Priority Mapping

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

**Dispatch Rules:**
- CRITICAL: Synchronous dispatch (caller blocks until all handlers return)
- HIGH/NORMAL/LOW: Asynchronous queue (event router task processes)

### 3.2 Queue Depths & Overflow Strategies

| Task | Queue Depth | Overflow Strategy |
|------|-------------|-------------------|
| sdf_match | 16 | Drop oldest (new match more relevant) |
| sdf_enroll | 8 | Block (enrollment is sequential) |
| sdf_admin | 8 | Drop (user will retry) |
| sdf_button | 4 | Drop (debounce handles burst) |
| sdf_zigbee | 32 | Block (ESP-ZB internal) |
| sdf_power | 8 | Drop (periodic) |

---

## 4. Migration Path

### Phase 0: Current State (Before refactor-services-task)
```
sdf_power_task (prio 4)
sdf_zigbee_task (prio 5)
sdf_services_task (prio 4) ──► Everything else (monolithic)
```

### Phase 1: Add Event Router (add-event-router)
```
sdf_power_task (prio 4)
sdf_zigbee_task (prio 5)
sdf_services_task (prio 4) ──► Emits/consumes via event router
sdf_event_router (library, no task)
```

### Phase 2: Split Services Task (refactor-services-task)
```
sdf_power_task (prio 4)
sdf_zigbee_task (prio 5)
sdf_match_task (prio 5)
sdf_enroll_task (prio 4)
sdf_admin_task (prio 5)
sdf_button_task (prio 4)
sdf_event_router (library)
```

### Phase 3: Future Tasks
```
... + sdf_ota_task (prio 3)
```

---

## 5. Stack Monitoring & Watchdog Assignments

### 5.1 Stack Monitoring

**API:**
```c
uint32_t sdf_task_get_stack_high_water_mark(sdf_task_id_t task_id);
```

**Task IDs:**
```c
typedef enum {
    SDF_TASK_POWER,
    SDF_TASK_ZIGBEE,
    SDF_TASK_MATCH,
    SDF_TASK_ENROLL,
    SDF_TASK_ADMIN,
    SDF_TASK_BUTTON,
    SDF_TASK_OTA,
} sdf_task_id_t;
```

**Validation Procedure (on hardware):**
1. Build with debug config: `idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.debug.defaults" build`
2. Flash and run
3. Exercise each task (match, enroll, admin, button, zigbee, power)
4. Call `sdf_task_get_stack_high_water_mark()` for each
5. Verify > 512 bytes free (12.5% margin on smallest stack)
6. Update this document with measured values

**Target Margins:**
| Task | Stack | Min Free | Target Free |
|------|-------|----------|-------------|
| sdf_power | 4 KB | 512 B | 1 KB |
| sdf_zigbee | 8 KB | 1 KB | 2 KB |
| sdf_match | 6 KB | 1 KB | 1.5 KB |
| sdf_enroll | 4 KB | 512 B | 1 KB |
| sdf_admin | 4 KB | 512 B | 1 KB |
| sdf_button | 3 KB | 384 B | 768 B |

### 5.2 Watchdog Assignments

| Task | Timeout | Type |
|------|---------|------|
| sdf_power | 5s | Task WDT (2× loop) |
| sdf_zigbee | 10s | Task WDT |
| sdf_match | 2s | Task WDT (5× poll) |
| sdf_enroll | 30s | Task WDT |
| sdf_admin | 15s | Task WDT |
| sdf_button | 5s | Task WDT |
| sdf_ota | 60s | Task WDT |

**Bootloader WDT:** 90s (enabled via `CONFIG_BOOTLOADER_WDT_ENABLE`)

---

## 6. Priority Inversion Analysis

### 6.1 Event Router Lock-Free Design

The event router uses lock-free ring buffers per priority level:
- No mutex in hot path (emit/consume)
- Priority-based dequeuing (CRITICAL → HIGH → NORMAL → LOW)
- No priority inversion possible on event delivery

### 6.2 Shared Resources

| Resource | Access Pattern | Protection |
|----------|----------------|------------|
| NVS | sdf_storage only | Mutex (low contention) |
| Fingerprint UART | sdf_match + sdf_enroll | Mutex in driver |
| LED driver | All tasks | Mutex (short hold) |
| BLE transport | sdf_app only | Single owner |
| Zigbee stack | sdf_zigbee only | Single owner |

### 6.3 Identified Risks

| Risk | Likelihood | Impact | Mitigation |
|------|------------|--------|------------|
| UART mutex contention (match vs enroll) | Medium | Match delayed | Match has HIGH prio; enroll NORMAL; match preempts |
| NVS mutex contention | Low | Delayed config write | Low frequency; short critical section |
| LED mutex contention | Low | Visual glitch | Very short hold; non-critical |

### 6.4 Conclusion

No unbounded priority inversion. All shared resources use priority-inheritance mutexes (FreeRTOS default) or are single-owner. Event router is lock-free.

---

## 7. Configuration Reference (Kconfig)

```kconfig
# sdf_power
CONFIG_SDF_POWER_TASK_PRIORITY=4
CONFIG_SDF_POWER_TASK_STACK=4096
CONFIG_SDF_POWER_LOOP_INTERVAL_MS=250
CONFIG_SDF_POWER_CHECKIN_INTERVAL_MS=15000
CONFIG_SDF_POWER_IDLE_BEFORE_SLEEP_MS=5000
CONFIG_SDF_POWER_POST_WAKE_GUARD_MS=1500
CONFIG_SDF_POWER_BATTERY_REPORT_INTERVAL_MS=60000
CONFIG_SDF_POWER_ENABLE_LIGHT_SLEEP=y
CONFIG_SDF_POWER_ENABLE_BLE_RADIO_GATING=y

# sdf_protocol_zigbee
CONFIG_SDF_ZIGBEE_TASK_PRIORITY=5
CONFIG_SDF_ZIGBEE_TASK_STACK=8192

# sdf_services (match/enroll/admin/button)
CONFIG_SDF_SVC_MATCH_TASK_PRIORITY=5
CONFIG_SDF_SVC_MATCH_TASK_STACK=6144
CONFIG_SDF_SVC_MATCH_POLL_INTERVAL_MS=400

CONFIG_SDF_SVC_ENROLL_TASK_PRIORITY=4
CONFIG_SDF_SVC_ENROLL_TASK_STACK=4096
CONFIG_SDF_SVC_ENROLL_MAX_RETRY_STEP1=3
CONFIG_SDF_SVC_ENROLL_MAX_RETRY_STEP2=3
CONFIG_SDF_SVC_ENROLL_MAX_RETRY_STEP3=0

CONFIG_SDF_SVC_ADMIN_TASK_PRIORITY=5
CONFIG_SDF_SVC_ADMIN_TASK_STACK=4096
CONFIG_SDF_SVC_ADMIN_TIMEOUT_MS=10000

CONFIG_SDF_SVC_BUTTON_TASK_PRIORITY=4
CONFIG_SDF_SVC_BUTTON_TASK_STACK=3072
CONFIG_SDF_SVC_BUTTON_DEBOUNCE_MS=50
CONFIG_SDF_SVC_BUTTON_LONG_PRESS_MS=1000
CONFIG_SDF_SVC_BUTTON_MULTI_WINDOW_MS=500

CONFIG_SDF_SVC_SECURITY_FAIL_THRESHOLD=5
CONFIG_SDF_SVC_SECURITY_FAIL_WINDOW_MS=60000
CONFIG_SDF_SVC_SECURITY_LOCKOUT_MS=120000

# sdf_ota (future)
CONFIG_SDF_OTA_TASK_PRIORITY=3
CONFIG_SDF_OTA_TASK_STACK=8192
CONFIG_SDF_OTA_CHUNK_SIZE=512
CONFIG_SDF_OTA_VERIFY_WRITE=y
```

---

## 8. Validation Checklist

- [ ] `doc/rtos_tasks.md` created with all 7 task specs
- [ ] `doc/sdf_sas.md` §6 updated with canonical task table
- [ ] `doc/software-architecture.md` §6 aligned with actual implementation
- [ ] `AGENTS.md` Component Structure lists all 6 tasks with owners
- [ ] Stack high-water marks measured on hardware
- [ ] Priority inversion analysis documented
- [ ] Watchdog timeouts configured per task
- [ ] Event router priority mapping verified
- [ ] Queue depths validated under load
- [ ] Migration path aligns with `add-event-router` and `refactor-services-task`

---

*Last updated: 2026-07-27*  
*Source of truth for task architecture — update on any task change*