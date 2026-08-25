# Spec: sdf_services Task Architecture

## Purpose

Split the monolithic `sdf_services_task` into focused FreeRTOS tasks communicating via the event router. Each task handles one responsibility with appropriate priority.

## Event Types (Extended sdf_common.h)

### New Event Types Added to sdf_event_router_type_t

```c
typedef enum {
    // Internal queue-only sentinel; rejected by subscribe() and emit()
    SDF_EVENT_ROUTER_INTERNAL_WAKE = 0,          // 0

    SDF_EVENT_ROUTER_BIOMETRIC_MATCH,            // 1
    SDF_EVENT_ROUTER_BIOMETRIC_MATCH_FAILED,     // 2
    SDF_EVENT_ROUTER_POWER_SLEEP,                // 3
    SDF_EVENT_ROUTER_POWER_WAKE,                 // 4
    SDF_EVENT_ROUTER_POWER_BATTERY,              // 5
    SDF_EVENT_ROUTER_SECURITY_LOCKOUT,           // 6
    SDF_EVENT_ROUTER_ADMIN_ACTION_REQUEST,       // 7
    SDF_EVENT_ROUTER_ENROLLMENT_STEP_COMPLETE,   // 8

    // Match cycle
    SDF_EVENT_ROUTER_BIOMETRIC_MATCH_REQUEST,    // 9 - Trigger match cycle

    // Enrollment
    SDF_EVENT_ROUTER_ENROLLMENT_START,           // 10 - Begin enrollment
    SDF_EVENT_ROUTER_ENROLLMENT_STEP_RESULT,     // 11 - Driver step result
    SDF_EVENT_ROUTER_ENROLLMENT_COMPLETE,        // 12 - All 3 steps done
    SDF_EVENT_ROUTER_ENROLLMENT_FAILED,          // 13 - Enrollment failed

    // Admin
    SDF_EVENT_ROUTER_ADMIN_AUTH_RESULT,          // 14 - Admin match result
    SDF_EVENT_ROUTER_ADMIN_ACTION_COMPLETE,      // 15 - Action executed

    // Web Companion
    SDF_EVENT_ROUTER_WEB_REG_AUTH_RESULT,        // 16 - Registration authorized

    // Button
    SDF_EVENT_ROUTER_BUTTON_PRESS,               // 17 - Short press
    SDF_EVENT_ROUTER_BUTTON_LONG_PRESS,          // 18 - Long press
    SDF_EVENT_ROUTER_BUTTON_MULTI_PRESS,         // 19 - Double/triple

    // Audit
    SDF_EVENT_ROUTER_AUDIT,                      // 20

    SDF_EVENT_ROUTER_TYPE_COUNT                  // 21
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
The `sdf_admin_task` and `sdf_services` SHALL support an "Admin Auth" state where `sdf_admin_task` waits for an Admin fingerprint scan to authorize a pending Web Registration request over BLE GATT. The authorization result SHALL be routed back to the GATT server for the originating connection. The registration decision (what user record to persist, and what reply to send) SHALL be computed by a pure `sdf_services` function, independent of the GATT transport that carries the request and reply. Raw web-registration credential material (name and password hash) SHALL NOT be carried in an event-router event payload or copied into any event-router queue at any point in this flow; it SHALL be written directly into `sdf_services`' owned pending-request state, and any component that needs it (including the routing of the authorization result back to the GATT server) SHALL read it back from that owned state rather than from an event payload.

When an Admin fingerprint scan authorizes a pending Web Registration request, the system SHALL capture the fingerprint user id of the matched admin into the same owned pending-request state, and the registration decision SHALL bind the persisted credential to that user id. The captured user id SHALL be subject to the same rule as the credential material: it SHALL NOT be carried in an event-router event payload.

The registration decision SHALL replace the credential of a user that already holds an account rather than creating a second account for that user, and SHALL refuse to persist a credential for which no authorizing admin user id was captured.

#### Scenario: Web Registration Authorized
- **WHEN** GATT server requests web registration authorization
- **AND** Admin finger is scanned successfully
- **THEN** system authorizes the registration
- **AND** GATT server saves credentials to NVS bound to the matched admin's user id
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
- **WHEN** the GATT server receives a Web Registration request containing a name and password hash
- **THEN** the name and password hash are written directly into `sdf_services`' owned pending-request state
- **AND** no event carrying the raw name or password hash is emitted or queued

#### Scenario: Authorizing admin identity bypasses the event router
- **WHEN** an Admin fingerprint scan authorizes a pending Web Registration request
- **THEN** the matched admin's user id is written into `sdf_services`' owned pending-request state
- **AND** no event carrying that user id is emitted or queued

#### Scenario: Registration result routing reads from owned state, not from an event payload
- **WHEN** `sdf_admin_task` resolves a pending Web Registration request (authorized or denied)
- **THEN** the name and bound user id used to route the result back to the originating GATT connection are read from `sdf_services`' owned pending-request state
- **AND** the event that signals the outcome does not itself carry the name or bound user id as a payload field

#### Scenario: Registration by an admin who already holds an account
- **WHEN** the registration decision runs for an admin whose user id already has a stored credential
- **THEN** the decision replaces that credential rather than allocating a second account
- **AND** the previous salt and stretched credential are not retained

#### Scenario: Registration without a captured authorizer is refused
- **WHEN** the registration decision runs with no authorizing admin user id in the pending-request state
- **THEN** the decision refuses the registration
- **AND** no credential is persisted

### Requirement: Web Login Verification
`sdf_services` SHALL expose a pure function that decides whether submitted BLE companion login credentials are valid, given a previously looked-up stored user record and a submitted password hash. The comparison SHALL use a constant-time algorithm with respect to the submitted hash value.

The permission that governs the resulting session SHALL NOT be taken from the stored account record. It SHALL be resolved from the enrolled-user record of the fingerprint user the account is bound to, at the time each authorization decision is made, so that a demotion or deletion of that user takes effect without the account being modified.

#### Scenario: Valid login credentials
- **WHEN** a submitted password hash matches the stored user's password hash exactly
- **THEN** the function reports the login as valid

#### Scenario: Invalid login credentials
- **WHEN** a submitted password hash does not match the stored user's password hash, or has an unexpected length
- **THEN** the function reports the login as invalid
- **AND** the comparison does not use an early-exit algorithm that could leak which byte first differed

#### Scenario: Session permission resolved from the bound user
- **WHEN** an authorization decision is made for an authenticated session
- **THEN** the permission used is read from the enrolled-user record of the bound fingerprint user
- **AND** it is not read from the stored account record

#### Scenario: Login refused when the bound user is no longer an admin
- **WHEN** a login is attempted against an account whose bound user's permission is no longer admin
- **THEN** the session is not granted admin authority

### Requirement: Double-Press Requests BLE Companion Pairing Window
The button task SHALL bind `BUTTON_DOUBLE_CLICK` to request the BLE Companion Service's admin-fingerprint-gated device pairing window, following the same `pending_admin_action` authorization flow used by every other admin action. This binding SHALL apply only once the device's setup-completion latch is set. While the device is in the setup phase, a button press SHALL instead reclaim the setup connection and re-arm the setup phase, and SHALL set no pending admin action.

#### Scenario: Double-click requests the pairing window
- **WHEN** a double-click occurs on the physical button
- **AND** the setup-completion latch is set
- **AND** no other admin action is currently pending
- **THEN** the system sets `pending_admin_action` to request the BLE Companion pairing window
- **AND** awaits an Admin fingerprint scan within the pending-action timeout, per the existing admin-fingerprint pending-action pattern

#### Scenario: Double-click ignored while another admin action is pending
- **WHEN** a double-click occurs
- **AND** `pending_admin_action` is already set to a different action
- **THEN** the double-click SHALL NOT change the pending action

#### Scenario: Double-click during the setup phase does not request a pairing window
- **WHEN** a double-click occurs while the setup-completion latch is unset
- **THEN** no pairing window is requested and no pending admin action is set
- **AND** the press is handled as a setup-phase reclaim/re-arm

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

### Requirement: Every Long-Running Service Task Participates In The Task Watchdog
Every long-running `sdf_services` task SHALL be registered with the task watchdog for the entire time it is running, and SHALL report liveness at least once per iteration of its main loop, so that a task which stops making progress trips the watchdog and reboots the device instead of failing silently. A task SHALL deregister from the watchdog as part of its cooperative shutdown, before it deletes itself, so that an intentionally stopped task is not reported as unresponsive.

This applies uniformly to the match, enrollment and admin tasks. The admin task is called out explicitly because it is the one that has never participated: a wedged admin task stops servicing button presses, admin action requests and admin action timeouts with no reboot and no diagnostic, which is indistinguishable from a hardware button fault.

The watchdog SHALL watch only the tasks that explicitly subscribe to it, and SHALL NOT watch the FreeRTOS idle tasks. Idle starvation is not evidence that a firmware task has wedged: on this unicore part the vendor radio stack runs an all-channel scan at a priority above idle and legitimately starves it for many seconds at a time, with every subscribed task healthy and reporting on schedule. Watching idle turns a normal radio scan into a panic-reboot loop while adding no coverage that subscription does not already provide.

#### Scenario: Admin task stops making progress
- **WHEN** the admin task stops returning to its main loop for longer than the configured watchdog timeout
- **THEN** the watchdog reports the admin task as unresponsive and the device reboots, rather than the device continuing to run with admin actions permanently unserviced

#### Scenario: Admin task is idle but healthy
- **WHEN** the admin task is running normally with no admin action pending and no events arriving
- **THEN** its bounded wait returns often enough that it reports liveness within the watchdog timeout, and the device does not reboot

#### Scenario: Service task is stopped on purpose
- **WHEN** a service task exits its main loop in response to a stop request and cleans up before deleting itself
- **THEN** it deregisters from the watchdog first, and its deliberate disappearance does not cause the watchdog to fire

#### Scenario: Watchdog participation is observable on the host test target
- **WHEN** the host test target starts the service tasks and then stops them
- **THEN** the tests can observe that each long-running service task became watchdog-registered while running and is no longer registered after stopping, so a future task that omits registration fails the suite rather than shipping unnoticed

#### Scenario: A higher-priority vendor task starves idle
- **WHEN** the vendor radio stack occupies the CPU at a priority above idle for longer than the watchdog timeout, while every subscribed service task continues to report liveness on schedule
- **THEN** the watchdog does not fire and the device does not reboot, because no subscribed task has stopped making progress

### Requirement: Last Remaining Admin Cannot Be Deleted

`sdf_services_delete_user()` SHALL reject deletion of a user whose permission is admin when that user is the only enrolled admin, and SHALL report the rejection distinctly from a sensor failure. The rejection SHALL be decided before any fingerprint sensor operation is issued, so that a refused delete costs no sensor round-trip and cannot leave the sensor and the cached enrolled-user record disagreeing.

The admin count SHALL be computed from the cached enrolled-user bitmap and packed permissions, consistent with `sdf_services_change_user_permission()`, and SHALL NOT issue a sensor query.

This guard protects the single admin-fingerprint gate on which every admin action depends. Losing the last admin leaves the pairing window, Enroll-Admin, Nuki re-pair, Zigbee join and Web Registration Authorization permanently unreachable, recoverable only by factory reset.

`sdf_services_clear_all_users()` SHALL NOT be subject to this guard. It is a deliberate bulk erase on the factory-reset path, where removing the last admin is the intended outcome.

#### Scenario: Deleting the only admin is refused

- **WHEN** a caller requests deletion of an enrolled user with admin permission
- **AND** that user is the only enrolled admin
- **THEN** the request is rejected
- **AND** no fingerprint sensor delete is issued
- **AND** the cached enrolled-user record is unchanged

#### Scenario: Deleting an admin while another admin remains succeeds

- **WHEN** a caller requests deletion of an enrolled user with admin permission
- **AND** at least one other enrolled user also has admin permission
- **THEN** the deletion proceeds
- **AND** the cached enrolled-user record and its NVS persistence are updated

#### Scenario: Deleting a non-admin user is unaffected

- **WHEN** a caller requests deletion of an enrolled user whose permission is not admin
- **AND** exactly one admin is enrolled
- **THEN** the deletion proceeds regardless of the admin count

#### Scenario: Admin count is read from the cache

- **WHEN** the guard evaluates how many admins are enrolled
- **THEN** the count is derived from the cached bitmap and packed permissions
- **AND** no fingerprint sensor query is issued to obtain it

#### Scenario: Clear-all is exempt

- **WHEN** all users are cleared through the bulk clear-all path
- **THEN** the operation proceeds even though it removes the last admin

### Requirement: User Deletion Validates Enrolment Before Touching The Sensor

`sdf_services_delete_user()` SHALL report a request to delete a user that is not present in the cached enrolled-user record as not-found, distinctly from a sensor failure, and SHALL NOT issue a fingerprint sensor delete for that user.

#### Scenario: Deleting an unenrolled user id

- **WHEN** a caller requests deletion of a user id that is not set in the cached enrolled-user bitmap
- **THEN** the request is reported as not found
- **AND** no fingerprint sensor delete is issued

#### Scenario: Not-found is distinguishable from sensor failure

- **WHEN** a caller receives a rejection for deleting an unenrolled user
- **THEN** the reported result differs from the result reported when the sensor rejects a delete for an enrolled user

### Requirement: User-Management Verbs Report A Named Outcome

`sdf_services`' user-management entry points SHALL report a named outcome for each verb rather than an `esp_err_t` whose values are shared across unrelated conditions. The outcome SHALL be decided where the condition is known, not decoded by the caller from an ambiguous code.

`ESP_ERR_INVALID_STATE` is currently returned by `sdf_services_delete_user()` and `sdf_services_change_user_permission()` for uninitialised services, an in-flight admin action, the last-admin guard, and an already-active enrolment. Each of those SHALL become separately reportable.

Callers SHALL NOT reconstruct the reason from their own context. A caller that today explains an ambiguous code by reasoning about what must have happened SHALL instead report what the services layer said.

#### Scenario: Last-admin refusal reported by name
- **WHEN** a user-management verb is refused because it would leave no enrolled admin
- **THEN** the reported outcome names that condition specifically

#### Scenario: Busy reported by name
- **WHEN** a user-management verb is refused because another admin action, permission change or enrolment is in flight
- **THEN** the reported outcome names that condition specifically
- **AND** it differs from the outcome reported for the last-admin refusal

#### Scenario: Occupied enrolment id reported by name
- **WHEN** an enrolment is requested for a user id that is already enrolled
- **THEN** the reported outcome names that condition specifically
- **AND** the check is performed in the services layer rather than by each caller

#### Scenario: Duplicate name reported by name
- **WHEN** a rename is refused because another enrolled user holds the target name
- **THEN** the reported outcome names that condition specifically

#### Scenario: Callers do not infer the reason
- **WHEN** a caller renders a refusal to a user
- **THEN** it renders the reported outcome
- **AND** it does not derive the reason from assumptions about its own preconditions

### Requirement: User Deletion Is An Admin-Fingerprint-Gated Action

Deleting an enrolled user on behalf of a remote request SHALL be authorized by a live admin fingerprint scan resolved through the pending-admin-action gate, in the same way as Enroll-Admin, Nuki re-pairing, Zigbee join and Web Registration Authorization.

The last-admin guard SHALL be evaluated before the gate is armed, so that a deletion that can never be permitted does not ask anyone to scan a finger.

The action SHALL resolve on denial and on timeout as well as on authorization, per "Pending BLE-Originated Admin Actions Always Resolve", and SHALL have an LED mapping, per "Pending Admin Action LED Mapping Is Complete".

#### Scenario: Deletion authorized by an admin scan
- **WHEN** a remote deletion request is made and an admin fingerprint scan authorizes it
- **THEN** the user is deleted

#### Scenario: Deletion denied by a non-admin scan
- **WHEN** the scan that resolves a pending deletion is not an enrolled admin
- **THEN** no user is deleted
- **AND** the request resolves with a denial

#### Scenario: Deletion times out
- **WHEN** no scan resolves a pending deletion before the pending-action window expires
- **THEN** no user is deleted
- **AND** the request resolves with a timeout

#### Scenario: Impossible deletion is refused before anyone is asked to scan
- **WHEN** a remote deletion request targets the only enrolled admin
- **THEN** the request is refused by the last-admin guard
- **AND** no pending admin action is armed
- **AND** no LED indication for a pending action is raised

### Requirement: Remote Enrolment Cannot Bypass The Admin Fingerprint Gate

An enrolment requested over a remote transport SHALL arm the admin-fingerprint gate and SHALL start the enrolment state machine only once an admin fingerprint scan has authorized it. An authenticated session SHALL NOT be sufficient on its own, at any permission level.

The existing local entry points that arm an enrolment directly SHALL remain available to the physical button path and to the setup phase, which have their own authorization: an admin scan already claimed by the button gesture, and the time-bounded setup phase on a device with no enrolled users.

#### Scenario: Remote enrolment waits for a scan
- **WHEN** an enrolment is requested over a remote transport
- **THEN** the enrolment state machine is not started
- **AND** it starts only after an admin fingerprint scan authorizes the request

#### Scenario: Remote enrolment of an admin is equally gated
- **WHEN** the requested enrolment carries admin permission
- **THEN** it is subject to the same authorizing scan as any other permission level

#### Scenario: Button path unchanged
- **WHEN** the physical button gesture resolves to an enrolment after its own admin scan
- **THEN** the enrolment starts without a second authorizing scan

#### Scenario: Setup-phase first enrolment unchanged
- **WHEN** the setup phase enrols the first user on a device with no enrolled users
- **THEN** the enrolment starts without an authorizing scan, since no admin exists to perform one