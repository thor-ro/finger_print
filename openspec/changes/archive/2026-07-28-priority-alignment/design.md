# Design: Align Task Priorities Between Documentation and Implementation

## Context

The Smart Door Finger firmware uses FreeRTOS on ESP32-C6 (single core). Task priorities determine preemption order:
- CRITICAL (6): Reserved for immediate-response security events
- HIGH (5): Lock actions, Zigbee commands, admin auth
- NORMAL (4): Power management, enrollment, button
- LOW (3): OTA, telemetry

## Decision

Set sdf_match and sdf_admin to HIGH (5), sdf_enroll and sdf_button to NORMAL (4).

## Rationale

1. **sdf_match at HIGH (5)**: Must preempt enrollment for lock actions but should yield to Zigbee commands (also HIGH, equal priority) and power management (NORMAL).
2. **sdf_enroll at NORMAL (4)**: Enrollment is non-urgent; can be preempted by match/admin operations.
3. **sdf_admin at HIGH (5)**: Must preempt match for admin actions (e.g., pairing, factory reset). Equal to match priority means either can preempt the other.
4. **sdf_button at NORMAL (4)**: Button presses trigger admin requests; NORMAL priority prevents button events from starving more critical tasks.

## Changes

### sdf_services_match.c
- Change `SDF_MATCH_TASK_PRIORITY` from 6 to 5

### sdf_services_enroll.c
- Change `SDF_ENROLL_TASK_PRIORITY` from 5 to 4

### sdf_services_admin.c
- Change `SDF_ADMIN_TASK_PRIORITY` from 6 to 5

### sdf_services_button.c
- Change `SDF_BUTTON_TASK_PRIORITY` from 5 to 4

### doc/rtos_tasks.md
- Verify all priority values match the code (no changes needed since doc was correct)

## Migration

No migration needed. This is a compile-time priority correction. Existing deployments will not be affected since the change only affects future builds.

## Risks

- **Preemption order change**: sdf_match and sdf_admin moving from CRITICAL to HIGH means they no longer preempt Zigbee tasks at the same priority level. However, both are HIGH now, so they can still interleave. The real risk was the opposite direction: CRITICAL (6) match/admin tasks starving Zigbee (5) and power (4) tasks.
- **Regression testing**: Should verify on hardware that lock action latency is still within 2s targets after the priority change.