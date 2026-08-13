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
The `sdf_services` component SHALL use static buffers sized for the maximum supported user count (10) rather than the sensor's hardware maximum (4095). Buffers SHALL use a compact bitmap + packed permissions representation to minimize RAM usage. This representation SHALL be persisted in `sdf_services_state_t` as the authoritative enrolled-user record; `sdf_services_query_users()` SHALL be served from this cached representation and SHALL NOT issue a sensor query.

#### Scenario: Query users for Zigbee sync
- **WHEN** `sdf_app_update_zigbee_user_list()` is called
- **THEN** the cached bitmap + packed permissions are read directly, with no sensor UART round trip
- **AND** user list is serialized to Zigbee attribute 0x4000

#### Scenario: Query users for local enrollment
- **WHEN** `sdf_services_start_local_enrollment_with_permission()` queries existing users
- **THEN** the cached bitmap of 16 bits tracking occupied IDs 1-10 is read directly, with no sensor UART round trip
- **AND** permissions are read from the packed 2-bit-per-user array

#### Scenario: Query users for permission change
- **WHEN** `sdf_services_change_user_permission()` queries existing users
- **THEN** the cached bitmap and packed permissions are read directly, with no sensor UART round trip
- **AND** admin count is computed from the cached packed permissions

### Requirement: Enrolled-User Cache Is Authoritative From Boot
The enrolled-user bitmap and packed permissions SHALL be persisted to NVS and loaded into `sdf_services_state_t` synchronously during `sdf_services_init()`, before any task that reads enrolled-user state is started. `enrolled_user_count` SHALL be computed as the population count of the cached bitmap rather than stored as an independently maintained field. No boot-time sensor query SHALL be required to determine enrolled-user state.

#### Scenario: Admin gate correct immediately after boot
- **WHEN** the device powers on with one or more users already enrolled
- **THEN** the enrolled-user count read by any task, including the very first button press after boot, reflects the persisted enrolled users
- **AND** admin-gated actions are never executed without fingerprint authorization due to a boot-time race

#### Scenario: Unclaimed device boots with zero users
- **WHEN** the device powers on with no persisted enrolled users
- **THEN** the enrolled-user count reads 0 immediately
- **AND** actions that require no prior admin (e.g. first enrollment) proceed without a fingerprint gate, consistent with unclaimed-device behavior

### Requirement: Synchronous NVS Persistence On Enrollment Mutation
Every successful change to enrolled-user state (enroll, delete, clear-all, permission change) SHALL update the in-memory cache and persist it to NVS before the operation is reported as successful to its caller.

#### Scenario: Enrollment persists before success is reported
- **WHEN** a fingerprint enrollment completes successfully on the sensor
- **THEN** the cache is updated and written to NVS
- **AND** enrollment is only reported complete to the caller after the NVS write succeeds

#### Scenario: Delete persists before success is reported
- **WHEN** a user is deleted from the sensor successfully
- **THEN** the cache is updated and written to NVS
- **AND** deletion is only reported complete to the caller after the NVS write succeeds

### Requirement: NVS Write Failure Handling
If the NVS write fails after a successful enrollment, the system SHALL retry the write with backoff; if retries are exhausted, the system SHALL roll back the sensor-side enrollment (delete the newly added print) and report the enrollment as failed, so the sensor and the persisted cache never disagree. If the NVS write fails after a successful delete, clear-all, or permission change, the system SHALL retry the write with backoff and report failure to the caller if retries are exhausted, without attempting a sensor-side rollback. In both cases, exhausting retries SHALL trigger the red LED error indication.

#### Scenario: Enrollment NVS write fails after retries exhausted
- **WHEN** an enrollment succeeds on the sensor but the NVS write fails on every retry
- **THEN** the newly enrolled print is deleted from the sensor
- **AND** enrollment is reported as failed to the caller
- **AND** the LED flashes red

#### Scenario: Delete NVS write fails after retries exhausted
- **WHEN** a user deletion succeeds on the sensor but the NVS write fails on every retry
- **THEN** deletion is reported as failed to the caller
- **AND** the LED flashes red

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

### Requirement: Web Registration Authorization
The `sdf_admin_task` and `sdf_services` SHALL support an "Admin Auth" state where `sdf_admin_task` waits for an Admin fingerprint scan to authorize a pending Web Registration request over BLE GATT. The authorization result SHALL be routed back to the GATT server for the originating connection. The registration decision (what user record to persist, and what reply to send) SHALL be computed by a pure `sdf_services` function, independent of the GATT transport that carries the request and reply. Raw web-registration credential material (username and password hash) SHALL NOT be carried in an event-router event payload or copied into any event-router queue at any point in this flow; it SHALL be written directly into `sdf_services`' owned pending-request state, and any component that needs it (including the routing of the authorization result back to the GATT server) SHALL read it back from that owned state rather than from an event payload.

#### Scenario: Web Registration Authorized
- **WHEN** GATT server requests web registration authorization
- **AND** Admin finger is scanned successfully
- **THEN** system authorizes the registration
- **AND** GATT server saves credentials to NVS
- **AND** GATT server marks the originating connection authenticated only after the credentials are saved

#### Scenario: Web Registration Denied
- **WHEN** GATT server requests web registration authorization
- **AND** non-Admin finger is scanned (or timeout occurs)
- **THEN** system denies the registration

#### Scenario: Pending registration always resolves
- **WHEN** an admin action completes with a result other than success while a Web Registration Authorization request is pending
- **THEN** the system SHALL resolve the pending request as denied
- **AND** the GATT server SHALL be notified so no BLE client is left waiting indefinitely

#### Scenario: Registration request credential material bypasses the event router
- **WHEN** the GATT server receives a Web Registration request containing a username and password hash
- **THEN** the username and password hash are written directly into `sdf_services`' owned pending-request state
- **AND** no event carrying the raw username or password hash is emitted or queued

#### Scenario: Registration result routing reads from owned state, not from an event payload
- **WHEN** `sdf_admin_task` resolves a pending Web Registration request (authorized or denied)
- **THEN** the username and permission used to route the result back to the originating GATT connection are read from `sdf_services`' owned pending-request state
- **AND** the event that signals the outcome does not itself carry the username or permission as a payload field

### Requirement: Web Login Verification
`sdf_services` SHALL expose a pure function that decides whether submitted BLE companion login credentials are valid, given a previously looked-up stored user record and a submitted password hash. The comparison SHALL use a constant-time algorithm with respect to the submitted hash value.

#### Scenario: Valid login credentials
- **WHEN** a submitted password hash matches the stored user's password hash exactly
- **THEN** the function reports the login as valid

#### Scenario: Invalid login credentials
- **WHEN** a submitted password hash does not match the stored user's password hash, or has an unexpected length
- **THEN** the function reports the login as invalid
- **AND** the comparison does not use an early-exit algorithm that could leak which byte first differed

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

### Requirement: Double-Press Requests BLE Companion Pairing Window
The button task SHALL bind `BUTTON_DOUBLE_CLICK` to request the BLE Companion Service's admin-fingerprint-gated device pairing window, following the same `pending_admin_action` authorization flow used by every other admin action.

#### Scenario: Double-click requests the pairing window
- **WHEN** a double-click occurs on the physical button
- **AND** no other admin action is currently pending
- **THEN** the system sets `pending_admin_action` to request the BLE Companion pairing window
- **AND** awaits an Admin fingerprint scan within the pending-action timeout, per the existing admin-fingerprint pending-action pattern

#### Scenario: Double-click ignored while another admin action is pending
- **WHEN** a double-click occurs
- **AND** `pending_admin_action` is already set to a different action
- **THEN** the double-click SHALL NOT change the pending action

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

### Requirement: Admin-Only Actions Not Bound To Physical Button Gestures
The button task SHALL NOT bind any gesture to `SDF_SERVICES_ADMIN_ACTION_ENROLL_ADMIN` or `SDF_SERVICES_ADMIN_ACTION_ZB_JOIN`. These actions SHALL only be reachable via an authenticated BLE Companion Service request.

#### Scenario: Triple-click produces no action
- **WHEN** a triple-click occurs on the physical button
- **THEN** no admin action is triggered
- **AND** `pending_admin_action` state is unaffected

#### Scenario: Hold-3s produces no action
- **WHEN** the button is held for 3 seconds
- **THEN** no admin action is triggered
- **AND** `pending_admin_action` state is unaffected

### Requirement: Simplified Pre-Enrollment Bootstrap Branch
On an unclaimed device (zero enrolled users), the button task's immediate-execution bootstrap path SHALL treat only `SDF_SERVICES_ADMIN_ACTION_ENROLL` as eligible for unauthenticated immediate execution. `SDF_SERVICES_ADMIN_ACTION_ENROLL_ADMIN` SHALL NOT reach this path, since it is no longer bound to any button gesture.

#### Scenario: Unclaimed device, single-click still enrolls immediately
- **WHEN** a single-click occurs
- **AND** the device has zero enrolled users
- **THEN** the system starts enrollment immediately, without requiring admin authorization