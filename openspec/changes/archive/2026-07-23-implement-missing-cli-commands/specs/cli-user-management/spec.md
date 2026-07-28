# CLI User Management Capability

## Overview
Full CRUD operations for fingerprint users via USB-C CLI, enabling local device provisioning and user management without a Zigbee coordinator.

## Requirements

### REQ-CLI-USER-001: List Users
**User Story**: As an installer, I want to list all enrolled fingerprint users so I can audit the device.
- **CLI**: `user list`
- **Output**: Table with columns: User ID, Permission (1=Standard, 2=Elevated, 3=Admin)
- **Backend**: `sdf_services_query_users()` → format as table
- **Auth**: Requires CLI login

### REQ-CLI-USER-002: Get User Details
**User Story**: As an installer, I want to view details for a specific user.
- **CLI**: `user get <user_id>`
- **Output**: `User ID: <id>, Permission: <level>, Enrolled: yes`
- **Backend**: `fp_query_user_permission(user_id, &perm)` + existence check
- **Error**: "User not found" if ID not enrolled
- **Auth**: Requires CLI login

### REQ-CLI-USER-003: Add User (Local Enrollment)
**User Story**: As an installer, I want to enroll a new user locally via CLI so I can provision devices without Zigbee.
- **CLI**: `user add <user_id> <permission>`
- **Constraints**: `user_id` 1-4095, `permission` 1-3
- **Flow**: 
  1. Validate user_id not occupied (query users)
  2. Trigger enrollment SM via `sdf_services_request_enrollment(user_id, permission)` 
  3. Prompt: "Place finger on sensor (scan 1 of 3)..."
  4. Call `fp_enroll_step(1, user_id, permission)` → wait for ACK
  5. Repeat for steps 2 and 3
  6. On success: "User <id> enrolled with permission <level>"
- **Auth**: Requires CLI login + admin fingerprint auth (via pending admin action)

### REQ-CLI-USER-004: Delete User
**User Story**: As an installer, I want to remove a user from the device.
- **CLI**: `user del <user_id>`
- **Backend**: `fp_delete_user(user_id)` → `sdf_services_delete_user(user_id)`
- **Output**: "User <id> deleted" or "User not found"
- **Auth**: Requires CLI login + admin fingerprint auth

### REQ-CLI-USER-005: Change Permission (Existing)
**User Story**: As an installer, I want to change a user's permission level.
- **CLI**: `user permission <user_id> <permission>` (already implemented)
- **Backend**: `sdf_services_change_user_permission()` — works via admin auth flow
- **Auth**: Requires CLI login + admin fingerprint auth

## Acceptance Criteria
- All commands require authenticated CLI session (`login` first)
- Admin actions (add, del, permission) require admin fingerprint within 10s
- Clear error messages for invalid IDs, occupied IDs, sensor errors
- Output format consistent: structured, human-readable
- Works in unclaimed state (0 users) for initial admin enrollment via `user add 1 3`

## Non-Functional
- Response time < 2s for query commands
- Enrollment flow matches button-initiated flow (3 scans, LED feedback)
- No new dependencies — uses existing `sdf_services` + `fingerprint` APIs