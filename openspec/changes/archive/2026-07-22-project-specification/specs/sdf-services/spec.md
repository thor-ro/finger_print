## ADDED Requirements

### Requirement: Fingerprint Match Polling Cycle
The `sdf_services` component SHALL run a periodic fingerprint match cycle: power on sensor, poll `fp_match_1n()` every 400ms, process result, power off sensor between polls.

#### Scenario: Normal match cycle
- **WHEN** `sdf_fp` task wakes (from deep sleep or timer)
- **THEN** Power on fingerprint sensor via GPIO EN
- **THEN** Call `fp_match_1n()` with 12s timeout
- **THEN** On match: invoke `match_cb(user_id, permission)` callback
- **THEN** On no match: invoke `match_cb(0, 0)` or error callback
- **THEN** Power off sensor, schedule next poll or sleep

#### Scenario: Match with pending admin action
- **WHEN** `pending_admin_action` is set (ENROLL, PAIR_NUKI, JOIN_ZIGBEE, FACTORY_RESET)
- **THEN** Fingerprint match result checked for `permission == 3` (Admin)
- **THEN** If admin: claim pending action, clear `pending_admin_action`, execute action
- **THEN** If non-admin: flash LED red, do not execute pending action
- **THEN** If no match: continue normal cycle

### Requirement: Enrollment Execution
The `sdf_services` component SHALL execute enrollment steps by driving the fingerprint sensor through 3-step enrollment and advancing the enrollment state machine.

#### Scenario: Three-step enrollment
- **WHEN** `sdf_services_request_enrollment(user_id, permission)` called
- **THEN** Call `fp_enroll_step(1, user_id, permission)` → on ACK_OK advance to step 2
- **THEN** Call `fp_enroll_step(2, user_id, permission)` → on ACK_OK advance to step 3
- **THEN** Call `fp_enroll_step(3, user_id, permission)` → on ACK_OK enrollment complete
- **THEN** On ACK_FAIL step 1-2: retry same step (user likely didn't lift finger)
- **THEN** On ACK_FAIL step 3: fail enrollment (templates incompatible)

#### Scenario: Enrollment LED feedback
- **WHEN** Step 1 ACK_OK: LED flash green
- **WHEN** Step 2 ACK_OK: LED flash green
- **WHEN** Step 3 ACK_OK: LED solid green (success)
- **WHEN** Any ACK_FAIL: LED flash red

### Requirement: Admin Authorization Cycle
The `sdf_services` component SHALL manage admin authorization: after button press sets pending action, wait for admin fingerprint (permission == 3) within 10s timeout.

#### Scenario: Successful admin auth
- **WHEN** `pending_admin_action` set, LED pulses blue (awaiting admin)
- **WHEN** Admin fingerprint matched (permission == 3) within 10s
- **THEN** LED admin auth green, claim action, execute requested operation
- **THEN** Clear `pending_admin_action`

#### Scenario: Admin auth timeout
- **WHEN** No admin fingerprint within 10 seconds
- **THEN** LED flash red, clear `pending_admin_action`, emit `SDF_AUDIT_AUTH_LOCKOUT`

#### Scenario: Non-admin tries to authorize
- **WHEN** Fingerprint matched but permission != 3 while `pending_admin_action` set
- **THEN** LED flash red, do not claim action, continue waiting for admin

### Requirement: Security Rate Limiting
The `sdf_services` component SHALL track failed biometric attempts and enforce lockout after configurable threshold.

#### Scenario: Failed attempt tracking
- **WHEN** `fp_match_1n()` returns no match (or error)
- **THEN** Increment `failed_attempt_count`, record `failed_attempt_window_start_us`
- **THEN** If `failed_attempt_count >= CONFIG_SDF_SECURITY_BIOMETRIC_FAIL_THRESHOLD` (default 5) within `CONFIG_SDF_SECURITY_BIOMETRIC_FAIL_WINDOW_MS` (default 60000ms)
- **THEN** Trigger lockout: `lockout_until_us = now + CONFIG_SDF_SECURITY_BIOMETRIC_LOCKOUT_MS` (default 120000ms)
- **THEN** Emit `SDF_AUDIT_AUTH_LOCKOUT` with lockout duration
- **THEN** Set Zigbee alarm bit 0x0004 (BIOMETRIC_LOCKOUT)

#### Scenario: Lockout enforcement
- **WHEN** Lockout active and match attempted
- **THEN** Reject immediately, do not power on sensor
- **THEN** LED flash red pattern
- **THEN** On lockout expiry: clear alarm bit, resume normal operation

### Requirement: LED Feedback Dispatch
The `sdf_services` component SHALL dispatch LED feedback patterns for all system states via `sdf_drivers_led_*()` calls.

#### Scenario: LED patterns
- **WHEN** Biometric match success: `led_flash_green()`
- **WHEN** Biometric match fail: `led_flash_red()`
- **WHEN** Admin auth success: `led_admin_auth_green()`
- **WHEN** Admin auth fail/timeout: `led_flash_red()`
- **WHEN** Enrollment step success: `led_flash_green()`
- **WHEN** Enrollment complete: `led_solid_green()`
- **WHEN** Unclaimed device: `led_breathe_white()`
- **WHEN** Awaiting admin: `led_pulse_blue()`
- **WHEN** Pairing active: `led_flash_yellow_fast()`
- **WHEN** Pairing success: `led_solid_green()`
- **WHEN** Zigbee join active: `led_pulse_cyan()`
- **WHEN** Low battery: `led_pulse_red_slow()`

### Requirement: Button Event Handling
The `sdf_services` component SHALL handle button events (short press, double press, long press) and map to admin actions.

#### Scenario: Button mappings
- **WHEN** Short press (< 500ms): `pending_admin_action = ENROLL`
- **WHEN** Double press (< 300ms between): `pending_admin_action = PAIR_NUKI`
- **WHEN** Long press (> 2000ms): `pending_admin_action = JOIN_ZIGBEE`
- **WHEN** Triple press: `pending_admin_action = FACTORY_RESET` (requires admin auth)
- **THEN** Each sets `pending_admin_action` and starts 10s admin auth window

### Requirement: User Management Callbacks
The `sdf_services` component SHALL provide callbacks for user management: query enrolled users, delete user, clear all users.

#### Scenario: Query enrolled users
- **WHEN** `sdf_services_get_enrolled_users(callback)` called
- **THEN** Call `fp_get_user_count()` and `fp_get_user_list()`
- **THEN** Invoke callback with user list (user_id, permission for each)

#### Scenario: Delete user
- **WHEN** `sdf_services_delete_user(user_id)` called
- **THEN** Call `fp_delete_user(user_id)`
- **THEN** On success: emit `SDF_AUDIT_USER_DELETED` with user_id

#### Scenario: Clear all users
- **WHEN** `sdf_services_clear_all_users()` called
- **THEN** Call `fp_delete_all_users()`
- **THEN** Emit `SDF_AUDIT_USERS_CLEARED`

### Requirement: FreeRTOS Task Definition
The `sdf_services` component SHALL define the `sdf_fp` task (single task for all fingerprint operations) with configurable stack size and priority.

#### Scenario: Task configuration
- **WHEN** `sdf_services_init()` called
- **THEN** Create task `sdf_fp` with stack `CONFIG_SDF_SERVICES_FP_TASK_STACK_SIZE` (default 6KB)
- **THEN** Priority `CONFIG_SDF_SERVICES_FP_TASK_PRIORITY` (default 5)
- **THEN** Task runs match cycle, enrollment execution, admin auth cycle