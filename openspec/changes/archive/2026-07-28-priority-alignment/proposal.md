# Proposal: Align Task Priorities Between Documentation and Implementation

## Summary

Fix critical priority mismatches between `doc/rtos_tasks.md` and the actual implemented priorities in `sdf_services_match.c`, `sdf_services_enroll.c`, and `sdf_services_admin.c`. The code has drifted from documented values, risking incorrect preemption behavior for lock actions and biometric matching.

## Problem

The documented task architecture (`doc/rtos_tasks.md`) specifies priority levels that no longer match the implementation:

| Task | Documented Priority | Actual Code Priority | Impact |
|------|---------------------|---------------------|--------|
| sdf_match | 5 (HIGH) | **6 (CRITICAL)** | Over-preempts everything; starves other tasks |
| sdf_enroll | 4 (NORMAL) | **5 (HIGH)** | Can block power/button tasks |
| sdf_admin | 5 (HIGH) | **6 (CRITICAL)** | Over-preempts Zigbee and power tasks |
| sdf_button | 4 (NORMAL) | **5 (HIGH)** | Can block during critical sections |

CRITICAL (6) is reserved for immediate-response events per `doc/rtos_tasks.md`, but match/admin tasks use it for routine lock operations. HIGH (5) is for lock actions and Zigbee commands, but enroll/button tasks use it too.

## Solution

Realign code priorities to match the documented architecture:

| Task | Target Priority | Rationale |
|------|----------------|-----------|
| sdf_match | **5 (HIGH)** | Must preempt enrollment but yield to Zigbee commands |
| sdf_enroll | **4 (NORMAL)** | Non-urgent; should yield to match/admin |
| sdf_admin | **5 (HIGH)** | Must preempt match for admin actions |
| sdf_button | **4 (NORMAL)** | Non-urgent; debounce handles burst |

## Architecture Impact

Only priority constants change in `firmware/components/sdf_services/src/sdf_services_match.c`, `sdf_services_enroll.c`, and `sdf_services_admin.c`. No API or behavioral changes beyond scheduling.

## Acceptance Criteria

- [ ] sdf_match priority = 5 (HIGH) in code
- [ ] sdf_enroll priority = 4 (NORMAL) in code
- [ ] sdf_admin priority = 5 (HIGH) in code
- [ ] sdf_button priority = 4 (NORMAL) in code
- [ ] doc/rtos_tasks.md updated to match any new values
- [ ] No priority inversion risk introduced
- [ ] Priority-to-name mapping in `doc/rtos_tasks.md` §6.2 reflects actual enum values