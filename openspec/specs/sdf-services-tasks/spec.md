# Spec: sdf_services Task Architecture

## Overview

Split the monolithic `sdf_services_task` into 4 focused FreeRTOS tasks communicating via the event router. Each task handles one responsibility with appropriate priority.

## Event Types (Extended sdf_common.h)

### New Event Types Added to sdf_event_router_type_t

```c
typedef enum {
    // Existing events...
    SDF_EVENT_ROUTER_BIOMETRIC_MATCH,           // 0
    SDF_EVENT_ROUTER_BIOMETRIC_MATCH_FAILED,    // 1
    SDF_EVENT_ROUTER_ZIGBEE_COMMAND,            // 2
    SDF_EVENT_ROUTER_BLE_LOCK_ACTION_COMPLETE,  // 3
    SDF_EVENT_ROUTER_POWER_SLEEP,               // 4
    SDF_EVENT_ROUTER_POWER_WAKE,                // 5
    SDF_EVENT_ROUTER_SECURITY_LOCKOUT,          // 6
    SDF_EVENT_ROUTER_ADMIN_ACTION_REQUEST,      // 7
    SDF_EVENT_ROUTER_ENROLLMENT_STEP_COMPLETE,  // 8

    // NEW: Match cycle
    SDF_EVENT_ROUTER_BIOMETRIC_MATCH_REQUEST,   // 9 - Trigger match cycle

    // NEW: Enrollment
    SDF_EVENT_ROUTER_ENROLLMENT_START,           // 10 - Begin enrollment
    SDF_EVENT_ROUTER_ENROLLMENT_STEP_RESULT,     // 11 - Driver step result
    SDF_EVENT_ROUTER_ENROLLMENT_COMPLETE,        // 12 - All 3 steps done
    SDF_EVENT_ROUTER_ENROLLMENT_FAILED,          // 13 - Enrollment failed

    // NEW: Admin
    SDF_EVENT_ROUTER_ADMIN_AUTH_RESULT,          // 14 - Admin match result
    SDF_EVENT_ROUTER_ADMIN_ACTION_COMPLETE,      // 15 - Action executed

    // NEW: Button
    SDF_EVENT_ROUTER_BUTTON_PRESS,               // 16 - Short press
    SDF_EVENT_ROUTER_BUTTON_LONG_PRESS,          // 17 - Long press
    SDF_EVENT_ROUTER_BUTTON_MULTI_PRESS,         // 18 - Double/triple
} sdf_event_router_type_t;
```

### New Payload Types

```c
// Match request - triggers a match cycle
typedef struct {
    uint32_t dummy;  // No payload needed, just trigger
} sdf_event_router_match_request_payload_t;

// Enrollment start
typedef struct {
    uint16_t user_id;
    uint8_t permission;
} sdf_event_router_enrollment_start_payload_t;

// Enrollment step result (from driver)
typedef struct {
    uint8_t step;
    sdf_fingerprint_op_result_t result;
    uint16_t user_id;
} sdf_event_router_enrollment_step_result_payload_t;

// Enrollment complete
typedef struct {
    uint16_t user_id;
    uint8_t permission;
    esp_err_t result;
} sdf_event_router_enrollment_complete_payload_t;

// Enrollment failed
typedef struct {
    uint16_t user_id;
    esp_err_t result;
} sdf_event_router_enrollment_failed_payload_t;

// Admin auth result
typedef struct {
    sdf_fingerprint_match_t match;
    bool authorized;
    sdf_services_admin_action_t pending_action;
} sdf_event_router_admin_auth_result_payload_t;

// Admin action complete
typedef struct {
    sdf_services_admin_action_t action;
    esp_err_t result;
} sdf_event_router_admin_action_complete_payload_t;

// Button press
typedef struct {
    uint8_t press_type;  // 1=single, 2=double, 3=triple, 4=long_3s, 5=long_8s
} sdf_event_router_button_payload_t;
```

### Extended Event Union

```c
typedef struct {
    sdf_event_router_type_t type;
    sdf_event_router_priority_t priority;
    uint32_t timestamp_ms;
    union {
        sdf_event_router_biometric_payload_t biometric;
        sdf_event_router_zigbee_payload_t zigbee;
        sdf_event_router_ble_payload_t ble;
        sdf_event_router_power_payload_t power;
        sdf_event_router_security_payload_t security;
        sdf_event_router_admin_payload_t admin;
        sdf_event_router_enrollment_payload_t enrollment;
        
        // NEW
        sdf_event_router_match_request_payload_t match_request;
        sdf_event_router_enrollment_start_payload_t enrollment_start;
        sdf_event_router_enrollment_step_result_payload_t enrollment_step_result;
        sdf_event_router_enrollment_complete_payload_t enrollment_complete;
        sdf_event_router_enrollment_failed_payload_t enrollment_failed;
        sdf_event_router_admin_auth_result_payload_t admin_auth_result;
        sdf_event_router_admin_action_complete_payload_t admin_action_complete;
        sdf_event_router_button_payload_t button;
    } payload;
} sdf_event_router_event_t;
```

## Task Specifications

### sdf_match_task (Priority: HIGH - 6)

**Core Loop:** 400ms poll interval

**Responsibilities:**
- Poll fingerprint sensor for matches
- Handle match cooldown
- Track failed attempts and lockout
- Emit match success/failed events

**Events Consumed:**
- `SDF_EVENT_ROUTER_BIOMETRIC_MATCH_REQUEST` - Trigger immediate match cycle
- `SDF_EVENT_ROUTER_POWER_WAKE` - Resume polling after wake
- `SDF_EVENT_ROUTER_POWER_SLEEP` - Stop polling, suspend task

**Events Emitted:**
- `SDF_EVENT_ROUTER_BIOMETRIC_MATCH` (priority HIGH) - Match succeeded
- `SDF_EVENT_ROUTER_BIOMETRIC_MATCH_FAILED` (priority HIGH) - No match/timeout
- `SDF_EVENT_ROUTER_SECURITY_LOCKOUT` (priority CRITICAL) - Lockout entered

**State:**
- `match_cooldown_until_us`
- `failed_attempt_count`, `failed_attempt_window_start_us`
- `lockout_until_us`
- `is_suspended` (power management)

**Behavior:**
```c
void sdf_match_task(void *arg) {
    // Init: subscribe to events
    // Wait for POWER_WAKE if starting suspended
    
    while (true) {
        // Check if we should run match cycle
        if (!is_suspended && !in_cooldown && !in_lockout && enrolled_users > 0) {
            // Reset WDT
            // fp_match_1n()
            // Handle result:
            //   - OK: emit MATCH, reset failed count, set cooldown
            //   - NO_MATCH/TIMEOUT: emit MATCH_FAILED, increment failed, check lockout
            //   - ERROR: emit MATCH_FAILED, set cooldown
        }
        
        // Wait for next poll interval OR event
        xQueueReceive(match_task_queue, &event, poll_interval_ms);
        // Handle events: MATCH_REQUEST, POWER_WAKE, POWER_SLEEP
    }
}
```

### sdf_enroll_task (Priority: NORMAL - 5)

**Core Loop:** Event-driven (no polling)

**Responsibilities:**
- Execute enrollment state machine steps
- Call driver enrollment functions
- Handle 3-step enrollment process
- Emit step complete/complete/failed events

**Events Consumed:**
- `SDF_EVENT_ROUTER_ENROLLMENT_START` - Start enrollment for user_id/permission
- `SDF_EVENT_ROUTER_ENROLLMENT_STEP_RESULT` - Driver result for current step
- `SDF_EVENT_ROUTER_POWER_WAKE` - Resume if suspended
- `SDF_EVENT_ROUTER_POWER_SLEEP` - Suspend

**Events Emitted:**
- `SDF_EVENT_ROUTER_ENROLLMENT_STEP_COMPLETE` (priority NORMAL) - Step done
- `SDF_EVENT_ROUTER_ENROLLMENT_COMPLETE` (priority HIGH) - All 3 steps done
- `SDF_EVENT_ROUTER_ENROLLMENT_FAILED` (priority HIGH) - Enrollment failed

**State:**
- Enrollment state machine (from sdf_enrollment_sm)
- Current user_id, permission
- Current step (1, 2, 3)

**Behavior:**
```c
void sdf_enroll_task(void *arg) {
    // Init: subscribe to events
    
    while (true) {
        xQueueReceive(enroll_task_queue, &event, portMAX_DELAY);
        
        switch (event.type) {
            case ENROLLMENT_START:
                // Init SM, set user_id/permission
                // Emit step 1 request (implicit - driver called directly)
                break;
            case ENROLLMENT_STEP_RESULT:
                // Feed result to SM
                // If step complete: call next step or emit COMPLETE
                // If failed: emit FAILED
                break;
            case POWER_SLEEP:
                suspend = true;
                break;
            case POWER_WAKE:
                suspend = false;
                break;
        }
    }
}
```

### sdf_admin_task (Priority: HIGH - 6)

**Core Loop:** Event-driven

**Responsibilities:**
- Wait for admin fingerprint match
- Verify admin permission (level 3)
- Execute pending admin action
- Handle admin action timeout

**Events Consumed:**
- `SDF_EVENT_ROUTER_ADMIN_ACTION_REQUEST` - Button pressed, action pending
- `SDF_EVENT_ROUTER_BIOMETRIC_MATCH` - Fingerprint match (filter for admin)
- `SDF_EVENT_ROUTER_POWER_WAKE` / `POWER_SLEEP`

**Events Emitted:**
- `SDF_EVENT_ROUTER_ADMIN_AUTH_RESULT` (priority HIGH) - Auth success/fail
- `SDF_EVENT_ROUTER_ADMIN_ACTION_COMPLETE` (priority HIGH) - Action done
- `SDF_EVENT_ROUTER_SECURITY_LOCKOUT` (if admin auth fails repeatedly)

**State:**
- `pending_admin_action` (NONE, ENROLL, ENROLL_ADMIN, NUKI_PAIR, ZB_JOIN, FACTORY_RESET, CHANGE_PERMISSION)
- `pending_admin_action_start_us`
- `admin_action_callback`, `admin_action_context`

**Behavior:**
```c
void sdf_admin_task(void *arg) {
    // Init: subscribe to events
    
    while (true) {
        xQueueReceive(admin_task_queue, &event, portMAX_DELAY);
        
        switch (event.type) {
            case ADMIN_ACTION_REQUEST:
                // Store action, start timeout timer
                // Emit LED indication based on action type
                break;
            case BIOMETRIC_MATCH:
                // If admin action pending AND match.permission == 3:
                //   Execute action, emit ADMIN_AUTH_RESULT(authorized=true)
                //   Emit ADMIN_ACTION_COMPLETE
                // Else if admin action pending:
                //   Emit ADMIN_AUTH_RESULT(authorized=false)
                //   Flash red LED
                break;
            case POWER_SLEEP/WAKE:
                // Handle suspend/resume
                break;
        }
        
        // Check timeout in loop or via timer event
        if (pending_action && now - start_us > ADMIN_TIMEOUT) {
            // Clear pending, emit ADMIN_AUTH_RESULT(authorized=false, timeout)
            // If CHANGE_PERMISSION: complete with TIMEOUT
        }
    }
}
```

### sdf_button_task (Priority: NORMAL - 5)

**Core Loop:** GPIO ISR + timer-based debounce

**Responsibilities:**
- Detect button presses (single, double, triple, long 3s, long 8s)
- Debounce handling
- Emit button events
- Handle unclaimed device (0 users) - immediate action

**Events Consumed:**
- `SDF_EVENT_ROUTER_POWER_WAKE` / `POWER_SLEEP`

**Events Emitted:**
- `SDF_EVENT_ROUTER_BUTTON_PRESS` (priority NORMAL) - Single press
- `SDF_EVENT_ROUTER_BUTTON_MULTI_PRESS` (priority NORMAL) - Double/triple
- `SDF_EVENT_ROUTER_BUTTON_LONG_PRESS` (priority NORMAL) - 3s/8s hold
- `SDF_EVENT_ROUTER_ADMIN_ACTION_REQUEST` (priority HIGH) - For claimed device

**State:**
- Button GPIO state
- Press timing state machine
- Debounce timer

**Behavior:**
```c
void sdf_button_task(void *arg) {
    // Init: configure GPIO interrupt, create debounce timer
    // Subscribe to POWER_WAKE/SLEEP
    
    while (true) {
        // Wait for ISR notification or timer
        xQueueReceive(button_task_queue, &event, portMAX_DELAY);
        
        // Process button state machine
        // On press detected: start debounce timer
        // On timer expiry: classify press type
        // Emit appropriate event
        
        // If 0 enrolled users: emit ADMIN_ACTION_REQUEST immediately
        // Else: emit BUTTON_PRESS/MULTI/LONG, admin task handles action request
    }
}

// ISR: just notify task (xTaskNotifyFromISR)
void IRAM_ATTR button_isr(void *arg) {
    xTaskNotifyFromISR(button_task_handle, 0, eNoAction, &higher_priority);
}
```

## Power Management Integration

All 4 tasks subscribe to `POWER_WAKE` / `POWER_SLEEP` events:

- **POWER_SLEEP**: Task suspends its core loop, stops polling, saves state
- **POWER_WAKE**: Task resumes, re-initializes sensor if needed, continues

Sensor power gating is handled by `sdf_drivers` via events:
- Match task emits `FP_POWER_OFF` when suspending (no users, cooldown, lockout)
- Power task (existing) handles actual GPIO control

## API Changes (sdf_services.h)

```c
// New public API
esp_err_t sdf_services_start_tasks(void);  // Creates all 4 tasks
esp_err_t sdf_services_stop_tasks(void);   // Deletes all 4 tasks

// Internal task functions (not public, in sdf_services_internal.h)
void sdf_match_task(void *arg);
void sdf_enroll_task(void *arg);
void sdf_admin_task(void *arg);
void sdf_button_task(void *arg);
```

## Task Configuration

| Task | Name | Stack | Priority | Core |
|------|------|-------|----------|------|
| sdf_match_task | sdf_match | 4096 | 6 (HIGH) | 0 |
| sdf_enroll_task | sdf_enroll | 4096 | 5 (NORMAL) | 0 |
| sdf_admin_task | sdf_admin | 4096 | 6 (HIGH) | 0 |
| sdf_button_task | sdf_btn | 3072 | 5 (NORMAL) | 0 |

Total stack: ~15KB (vs 8KB single task)

## Acceptance Criteria

- [ ] All 4 tasks created with correct priorities
- [ ] Match cycle runs independently at 400ms
- [ ] Enrollment executes 3 steps via events
- [ ] Admin auth waits for fingerprint via events
- [ ] Button presses emit correct events with debounce
- [ ] All existing unit tests pass
- [ ] Integration test: fingerprint → unlock → BLE action
- [ ] Memory usage < 20KB total task stacks
- [ ] Power management: tasks suspend/resume on events

## User Capacity and Buffer Optimization

### Requirement: User Capacity
The fingerprint sensor SHALL support User IDs from `1` to `10`. When a new user is enrolled locally, the firmware SHALL automatically assign the lowest available User ID. If all User IDs are occupied, the LED SHALL flash **red** and enrollment SHALL be rejected.

#### Scenario: Enroll user within capacity
- **WHEN** device has < 10 enrolled users and enrollment is initiated
- **THEN** user is enrolled with lowest available User ID (1-10)
- **AND** LED flashes **green** on each successful scan
- **AND** LED shows solid **green** on completion

#### Scenario: Enroll user at capacity
- **WHEN** device has 10 enrolled users and enrollment is initiated
- **THEN** enrollment is rejected
- **AND** LED flashes **red**

#### Scenario: Automatic User ID assignment finds gaps
- **WHEN** users 1, 2, 4, 5 are enrolled (user 3 was deleted)
- **THEN** next enrollment assigns User ID 3 (lowest available)

### Requirement: User Query Buffer Sizing
The `sdf_services` component SHALL use static buffers sized for the maximum supported user count (10) rather than the sensor's hardware maximum (4095). Buffers SHALL use a compact bitmap + packed permissions representation to minimize RAM usage.

#### Scenario: Query users for Zigbee sync
- **WHEN** `sdf_app_update_zigbee_user_list()` is called
- **THEN** static buffers of 10 entries are used (no dynamic allocation)
- **AND** user list is serialized to Zigbee attribute 0x4000

#### Scenario: Query users for local enrollment
- **WHEN** `sdf_services_start_local_enrollment_with_permission()` queries existing users
- **THEN** static buffer of 10 entries is used
- **AND** bitmap of 16 bits tracks occupied IDs 1-10
- **AND** permissions packed as 2-bit values in uint8_t array

#### Scenario: Query users for permission change
- **WHEN** `sdf_services_change_user_permission()` queries existing users
- **THEN** static buffer of 10 entries is used
- **AND** admin count is computed from packed permissions

### Requirement: Static RAM Buffer Optimization (Optimization #16)
The system SHALL implement the documented optimization #16: replace 3072-byte static buffers with bitmap + packed permissions representation achieving ~99.6% RAM savings.

#### Scenario: Enrollment query buffer
- **WHEN** `sdf_services_start_local_enrollment_with_permission()` runs
- **THEN** uses `uint16_t user_ids[10]` + `uint8_t permissions[10]` = 30 bytes
- **AND** bitmap `uint16_t occupied_ids = 0` (1 bit per ID 1-10)
- **AND** packed permissions `uint8_t packed_perms[4]` (2 bits per user)

#### Scenario: Permission change query buffer
- **WHEN** `sdf_services_change_user_permission()` runs
- **THEN** uses same compact representation
- **AND** admin count computed by iterating packed 2-bit permissions

### Requirement: CLI User ID Validation
The CLI commands SHALL validate User IDs against the new maximum of 10.

#### Scenario: User add with valid ID
- **WHEN** `user add 5 1` is executed
- **THEN** command accepts ID 5 (within 1-10 range)

#### Scenario: User add with invalid ID
- **WHEN** `user add 11 1` is executed
- **THEN** command rejects with "Invalid user_id. Expected 1-10."

#### Scenario: User get with invalid ID
- **WHEN** `user get 15` is executed
- **THEN** command rejects with "Invalid user_id. Expected 1-10."

#### Scenario: User del with invalid ID
- **WHEN** `user del 20` is executed
- **THEN** command rejects with "Invalid user_id. Expected 1-10."

#### Scenario: User permission with invalid ID
- **WHEN** `user permission 12 3` is executed
- **THEN** command rejects with "Invalid user_id. Expected 1-10."