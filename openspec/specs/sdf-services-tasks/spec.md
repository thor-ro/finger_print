# Spec: sdf_services Task Architecture

## Purpose

Split the monolithic `sdf_services_task` into focused FreeRTOS tasks communicating via the event router. Each task handles one responsibility with appropriate priority.

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

// Admin action request
typedef struct {
    uint8_t action;
    uint8_t origin;
} sdf_event_router_admin_payload_t;

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

## Requirements

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
On an unclaimed device (zero enrolled users), the admin-action authorization path's immediate-execution bootstrap branch SHALL route `SDF_SERVICES_ADMIN_ACTION_ENROLL` directly into local enrollment, and SHALL route every other button-reachable action into the configured admin-action execution callback. `SDF_SERVICES_ADMIN_ACTION_ENROLL_ADMIN` SHALL NOT reach this path at all, since it is no longer bound to any button gesture.

#### Scenario: Unclaimed device, single-click still enrolls immediately
- **WHEN** a single-click occurs
- **AND** the device has zero enrolled users
- **THEN** the system starts enrollment immediately, without requiring admin authorization

#### Scenario: Unclaimed device, a non-enroll button action executes immediately
- **WHEN** a button gesture bound to an admin action other than `SDF_SERVICES_ADMIN_ACTION_ENROLL` occurs
- **AND** the device has zero enrolled users
- **THEN** the action is executed immediately via the admin-action execution callback, without admin-fingerprint authorization
- **AND** no pending admin action is left set

#### Scenario: Admin-only action cannot reach the bootstrap branch
- **WHEN** the device has zero enrolled users
- **THEN** `SDF_SERVICES_ADMIN_ACTION_ENROLL_ADMIN` is not reachable by any button gesture and therefore never enters the bootstrap branch

### Requirement: Unauthenticated Bootstrap Bypass Is Restricted To Local Physical Origin
The zero-enrolled-users bootstrap bypass — executing an admin action without admin-fingerprint authorization because no admin exists to give it — SHALL be granted only to requests originating from a physical interaction with the device. A remotely-originated admin action request SHALL NOT be granted the bypass, regardless of how many users are enrolled.

#### Scenario: Local physical request on an unclaimed device
- **WHEN** an admin action is requested by physical interaction with the device
- **AND** the device has zero enrolled users
- **THEN** the action executes immediately without admin-fingerprint authorization

#### Scenario: Remote request on an unclaimed device
- **WHEN** an admin action is requested remotely
- **AND** the device has zero enrolled users
- **THEN** the request SHALL NOT execute without authorization
- **AND** it follows the ordinary admin-fingerprint pending-action flow, which cannot be satisfied while no admin exists

#### Scenario: Local physical request on a claimed device
- **WHEN** an admin action is requested by physical interaction with the device
- **AND** the device has at least one enrolled user
- **THEN** the bypass does not apply and the request follows the ordinary admin-fingerprint pending-action flow

### Requirement: Bootstrap Bypass Decision Is Single-Sited
The decision of whether a given admin action request may execute without admin-fingerprint authorization SHALL be made in one place, consulted by every path that can set or execute an admin action, rather than being reimplemented per request path.

#### Scenario: A new request path is introduced
- **WHEN** a new path for requesting an admin action is added
- **THEN** it obtains its authorization decision from the same single decision point as every existing path
- **AND** it cannot grant the bypass without passing an explicit request origin to that decision point

#### Scenario: Bypass is taken
- **WHEN** the single decision point grants the bootstrap bypass
- **THEN** any previously pending admin action state is cleared before the action executes, so the bypassed action does not leave a stale pending action behind

### Requirement: Pending Admin Action LED Indication Is Path-Independent
When an admin action becomes pending (awaiting admin-fingerprint authorization), the system SHALL produce the LED indication assigned to that action, and that indication SHALL be identical regardless of which request path caused the action to become pending (physical button gesture, event-router admin-action request, or direct authenticated request).

#### Scenario: Same action, different origin, same indication
- **WHEN** a given admin action becomes pending via one request path
- **AND** the same admin action later becomes pending via a different request path
- **THEN** the LED indication is the same in both cases

#### Scenario: BLE Companion pairing window becomes pending
- **WHEN** `SDF_SERVICES_ADMIN_ACTION_BLE_PAIRING_WINDOW` becomes the pending admin action
- **THEN** the system produces its assigned LED indication, on every path that can set it pending

#### Scenario: Action with no assigned indication
- **WHEN** an admin action that has no assigned LED indication becomes pending
- **THEN** the system produces no LED indication for it, and does not fall back to another action's indication

### Requirement: Pending Admin Action LED Mapping Is Complete
The system SHALL define an LED indication for every admin action that can be set pending, so that the device never enters an awaiting-admin-fingerprint state without user-visible feedback that it is waiting.

#### Scenario: Every settable pending action has feedback
- **WHEN** any admin action is set as `pending_admin_action`
- **THEN** the user receives an LED indication that the device is awaiting admin authorization

### Requirement: Button Handling Requires No Dedicated Task
Button press detection, classification, and dispatch SHALL be driven entirely by the button driver's own scan mechanism and its registered callbacks. The system SHALL NOT run a dedicated FreeRTOS task for button handling, and SHALL NOT consume a task stack, event queue, or event-router subscription for that purpose.

#### Scenario: No periodic button task wakeup
- **WHEN** the system is idle with no button press in progress
- **THEN** no button-related task wakes periodically, because no button task exists

#### Scenario: Press still detected and dispatched
- **WHEN** the button is physically pressed
- **THEN** the press is classified (single/double/long) and the corresponding action is dispatched, with the same resulting behavior as before the task was removed

#### Scenario: Button lifecycle follows service start/stop
- **WHEN** `sdf_services_start_tasks()` succeeds and later `sdf_services_stop_tasks()` is called
- **THEN** button handling is initialized on start and torn down on stop
- **AND** a subsequent start reinitializes button handling, so a stop/start cycle leaves the button functional

### Requirement: Enrollment Button Scan Quiesces When Idle
The enrollment button's GPIO scan SHALL stop running on a periodic timer once no press is in progress, and SHALL resume automatically on the next GPIO interrupt for that button, rather than scanning continuously regardless of press state.

#### Scenario: No press in progress
- **WHEN** the enrollment button has not been pressed and no debounce/press sequence is in progress
- **THEN** periodic scanning stops until the next physical press

#### Scenario: Press interrupts idle scanning
- **WHEN** the button is physically pressed while scanning is stopped
- **THEN** scanning resumes and the press is detected and classified normally (single/double/long-press)

### Requirement: Idle Service Task Loops Use Bounded Blocking Waits
When they have no work pending and no deadline sooner than their wait cap, `sdf_enroll_task` and `sdf_admin_task` SHALL block waiting for incoming events rather than run a fixed-interval poll, waking only as often as required to service any task watchdog registration they hold.

#### Scenario: No enrollment activity pending
- **WHEN** `sdf_enrollment_sm_is_active()` is false and no relevant event has arrived
- **THEN** `sdf_enroll_task` remains blocked, waking at most at its watchdog-safe cadence rather than on a fixed short poll interval

#### Scenario: No admin action pending
- **WHEN** no admin action is pending and no relevant event has arrived
- **THEN** `sdf_admin_task` remains blocked, waking at most at its wait cap rather than on a fixed short poll interval

#### Scenario: Idle tasks do not cap the automatic light-sleep window
- **WHEN** the system is idle with no enrollment active and no admin action pending
- **THEN** no service task in this capability wakes on a sub-second fixed interval

### Requirement: Pending Admin Action Timeout Is Deadline-Driven
`sdf_admin_task` SHALL detect expiry of the pending-admin-action timeout by waiting until that timeout's deadline, rather than by re-checking it on a fixed short interval. The timeout duration itself SHALL be unchanged; only the granularity with which expiry is noticed may loosen, bounded by the task's wait cap.

#### Scenario: An admin action is pending
- **WHEN** an admin action is pending
- **THEN** `sdf_admin_task`'s wait targets that action's expiry deadline, clamped to its wait cap

#### Scenario: Pending action expires
- **WHEN** the pending-admin-action timeout elapses
- **THEN** the action is cleared, the timeout indication is produced, and the action-complete notification is emitted, as before this change

#### Scenario: Pending action is authorized before expiry
- **WHEN** a pending action is authorized before its deadline
- **THEN** no timeout occurs and the task returns to its idle blocking wait

### Requirement: Setting A Pending Admin Action Wakes The Admin Task
Because a pending admin action can be set by callers that publish no event, setting `pending_admin_action` SHALL cause `sdf_admin_task` to re-evaluate its wait, so that the action's timeout countdown begins at the deadline it was actually set for rather than at the task's next scheduled wake.

#### Scenario: Pending action set by a non-publishing caller
- **WHEN** a pending admin action is set by a caller that emits no event
- **THEN** `sdf_admin_task` wakes and recomputes its wait against the new deadline

#### Scenario: Pending action set while the task is in a long idle wait
- **WHEN** a pending admin action is set while `sdf_admin_task` is blocked on its full wait cap
- **THEN** the task does not wait out the remaining cap before beginning to track the new deadline

### Requirement: Active Enrollment Retries On A State-Driven Cadence
While an enrollment is active, `sdf_enroll_task` SHALL retry the current step at a cadence driven by that step's own retry policy, rather than by an unconditional fixed-interval poll that also runs while idle.

#### Scenario: Step requires a retry
- **WHEN** the enrollment state machine reports a retryable step result
- **THEN** the next attempt for that step occurs on the step's own retry cadence, not on the idle-loop's poll interval

#### Scenario: Enrollment becomes idle again
- **WHEN** an enrollment completes or fails
- **THEN** `sdf_enroll_task` returns to the bounded blocking wait behavior of an idle task

### Requirement: Shutdown Signal Is Pushed, Not Polled
`sdf_services_stop_tasks()` SHALL deliver the stop request to every task whose loop wait was lengthened by this change — `sdf_enroll_task` and `sdf_admin_task` — in a way that wakes the task immediately, rather than requiring it to observe `stop_requested` only at its next periodic poll.

#### Scenario: Stop requested while the enroll task is idle and blocked
- **WHEN** `sdf_services_stop_tasks()` is called while `sdf_enroll_task` is blocked waiting for events
- **THEN** the task wakes immediately, unwinds, and clears its task handle without waiting for a periodic timeout to elapse

#### Scenario: Stop requested while the admin task is idle and blocked
- **WHEN** `sdf_services_stop_tasks()` is called while `sdf_admin_task` is blocked waiting for events
- **THEN** the task wakes immediately, unwinds, and clears its task handle without waiting for a periodic timeout to elapse

#### Scenario: Stop signal is lost
- **WHEN** the pushed stop signal cannot be delivered to a task
- **THEN** the task still observes `stop_requested` at its next wait-cap expiry, so shutdown completes within the existing overall stop budget

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

### Requirement: Service task subscriptions are registered before task creation
The match, enroll, and admin tasks SHALL NOT register their event-router subscriptions from inside their own task bodies. Service initialization SHALL register every subscription these tasks depend on before the corresponding task is created, so that no subscription can be registered concurrently with an event dispatch.

#### Scenario: Subscriptions exist before the task runs
- **WHEN** service initialization creates the match, enroll, or admin task
- **THEN** that task's subscriptions are already registered, and the task's first loop iteration can receive any event of a subscribed type

#### Scenario: No event is missed during task startup
- **WHEN** an event of a subscribed type is emitted between service initialization and the task's first loop iteration
- **THEN** the event is delivered to the task's queue rather than being lost because the subscription had not been registered yet

### Requirement: Service tasks do not deregister subscriptions on shutdown
Cooperative task shutdown SHALL NOT deregister event-router subscriptions. A task that exits its loop on `stop_requested` SHALL leave its subscriptions in place and SHALL ensure its callback tolerates being invoked after the task has exited, by discarding events when its queue is no longer serviceable.

#### Scenario: Shutdown leaves subscriptions registered
- **WHEN** a service task observes `stop_requested` and unwinds
- **THEN** it performs no subscription teardown before deleting itself

#### Scenario: Callback after task exit is harmless
- **WHEN** an event of a subscribed type is dispatched after the owning task has exited
- **THEN** the subscriber callback discards the event without dereferencing a torn-down queue and without crashing