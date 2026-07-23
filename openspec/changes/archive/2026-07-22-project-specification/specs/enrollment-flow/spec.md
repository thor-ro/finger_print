## ADDED Requirements

### Requirement: First-Time Setup (Unclaimed Device)
The enrollment flow SHALL support first-time setup on an unclaimed device (0 enrolled users).

#### Scenario: Unclaimed device state
- **WHEN** Device boots with 0 enrolled users
- **THEN** LED breathes WHITE continuously
- **THEN** Device state = UNCLAIMED

#### Scenario: Admin enrollment (first user)
- **WHEN** User short-presses configuration button
- **THEN** LED pulses BLUE (awaiting enrollment)
- **THEN** `sdf_services_request_enrollment(user_id=1, permission=3)` called automatically
- **THEN** No admin authorization required (first user is always admin)
- **WHEN** User places finger 3 times successfully
- **THEN** LED flashes GREEN after each scan
- **THEN** LED solid GREEN on completion
- **THEN** Device state = CLAIMED, enrolled_user_count = 1
- **THEN** User ID 1 created with Admin permission (3)

### Requirement: Local Standard User Enrollment (Claimed Device)
The enrollment flow SHALL support local enrollment of standard users on a claimed device.

#### Scenario: Standard user enrollment
- **WHEN** Device is CLAIMED (>0 enrolled users)
- **WHEN** User short-presses configuration button
- **THEN** LED pulses BLUE (awaiting admin auth)
- **THEN** Admin has 10 seconds to authorize with fingerprint
- **WHEN** Admin fingerprint matched (permission == 3)
- **THEN** LED flashes GREEN (admin auth success)
- **THEN** `sdf_services_request_enrollment(lowest_available_id, permission=1)` called
- **WHEN** New user places finger 3 times
- **THEN** LED flashes GREEN after each scan
- **THEN** LED solid GREEN on completion
- **THEN** User enrolled with lowest available ID, permission = 1 (Standard)

#### Scenario: Admin auth timeout
- **WHEN** No admin fingerprint within 10 seconds
- **THEN** LED flashes RED, enrollment cancelled
- **THEN** Device returns to idle

#### Scenario: Non-admin tries to authorize
- **WHEN** Fingerprint matched but permission != 3 during admin auth window
- **THEN** LED flashes RED, authorization denied
- **THEN** Continue waiting for admin (timer not reset)

### Requirement: Local Admin Enrollment
The enrollment flow SHALL support enrolling additional Admin users.

#### Scenario: Triple-press admin enrollment
- **WHEN** User triple-presses configuration button rapidly (< 300ms between)
- **THEN** LED pulses BLUE (awaiting admin auth)
- **THEN** Admin authorizes (same as standard enrollment)
- **THEN** `sdf_services_request_enrollment(lowest_available_id, permission=3)` called
- **WHEN** New admin places finger 3 times
- **THEN** User enrolled with permission = 3 (Admin)
- **THEN** Warning: New admin has full device control

### Requirement: Remote Enrollment via Zigbee
The enrollment flow SHALL support remote enrollment triggered by Zigbee programming commands.

#### Scenario: Set PIN Code enrollment
- **WHEN** Zigbee `Set PIN Code` command received (cluster 0x0101, cmd 0x08)
- **THEN** Extract user_id, pin (ignored), permission from command
- **THEN** `sdf_app` queues enrollment request
- **THEN** No admin authorization required (trusted network)
- **THEN** LED flashes BLUE (enrollment mode active)
- **WHEN** User places finger 3 times
- **THEN** LED flashes GREEN per scan, solid GREEN on success
- **THEN** Result reported to Zigbee coordinator via attribute 0x4000

#### Scenario: Set RFID Code enrollment
- **WHEN** Zigbee `Set RFID Code` command received (cluster 0x0101, cmd 0x12)
- **THEN** Same as PIN Code enrollment (RFID data ignored, triggers fingerprint enrollment)

#### Scenario: Clear PIN Code (delete user)
- **WHEN** Zigbee `Clear PIN Code` command received (cmd 0x09)
- **THEN** `sdf_services_delete_user(user_id)` called
- **THEN** User removed from fingerprint sensor
- **THEN** Attribute 0x4000 updated

#### Scenario: Clear All PIN Codes
- **WHEN** Zigbee `Clear All PIN Codes` command received (cmd 0x0A)
- **THEN** `sdf_services_clear_all_users()` called
- **THEN** All users removed
- **THEN** Device returns to UNCLAIMED state (LED breathes WHITE)

### Requirement: Enrollment LED Feedback
The enrollment flow SHALL provide clear LED feedback for each step.

#### Scenario: LED patterns
- **WHEN** Awaiting admin auth: LED pulses BLUE
- **WHEN** Admin auth success: LED flashes GREEN once
- **WHEN** Enrollment step 1 success: LED flashes GREEN
- **WHEN** Enrollment step 2 success: LED flashes GREEN
- **WHEN** Enrollment step 3 success: LED solid GREEN (3 seconds)
- **WHEN** Enrollment step fail (1-2): LED flashes RED, retry same step
- **WHEN** Enrollment step 3 fail: LED flashes RED, enrollment FAILED
- **WHEN** Admin auth timeout: LED flashes RED
- **WHEN** Unclaimed device: LED breathes WHITE

### Requirement: User ID Assignment
The enrollment flow SHALL automatically assign the lowest available User ID.

#### Scenario: ID assignment
- **WHEN** Enrolling new user locally
- **THEN** Scan User IDs 1-500 for first gap
- **THEN** Assign that ID
- **WHEN** All 500 IDs occupied
- **THEN** LED flashes RED, enrollment rejected

### Requirement: Permission Model Enforcement
The enrollment flow SHALL enforce the permission model.

#### Scenario: Permission levels
- **WHEN** Permission = 1 (Standard): Can unlock door only
- **WHEN** Permission = 2 (Elevated): Reserved, same as Standard
- **WHEN** Permission = 3 (Admin): Can unlock + authorize config actions
- **THEN** First user always permission 3
- **THEN** Local standard enrollment always permission 1
- **THEN** Local admin enrollment (triple press) always permission 3
- **THEN** Remote enrollment: permission from Zigbee command

### Requirement: Enrollment State Machine Integration
The enrollment flow SHALL integrate with the enrollment state machine.

#### Scenario: State machine coordination
- **WHEN** Enrollment triggered
- **THEN** `sdf_enrollment_sm_start(user_id, permission)` called
- **WHEN** Each fingerprint scan
- **THEN** `fp_enroll_step(step, user_id, permission)` called
- **THEN** On ACK: `sdf_enrollment_sm_apply_step_result(FP_ACK_OK)`
- **THEN** On FAIL (step 1-2): `sdf_enrollment_sm_apply_step_result(FP_ACK_FAIL)` → retry
- **THEN** On FAIL (step 3): `sdf_enrollment_sm_apply_step_result(FP_ACK_FAIL)` → ERROR
- **WHEN** State = SUCCESS: enrollment complete, user saved
- **WHEN** State = ERROR: enrollment failed, cleanup