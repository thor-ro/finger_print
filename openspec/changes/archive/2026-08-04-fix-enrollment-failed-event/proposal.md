## Why

`sdf_app_init()` subscribes to `SDF_EVENT_ROUTER_ENROLLMENT_FAILED` but the `sdf_app_on_event()` switch-case has no handler for it. Enrollment failures are silently dropped: no alarm bit is set on the Zigbee stack, no audit event is emitted, no LED feedback is triggered. The user and coordinator have no indication that enrollment failed.

## What Changes

- Add a `case SDF_EVENT_ROUTER_ENROLLMENT_FAILED:` handler in `sdf_app_on_event()`
- On enrollment failed: set `SDF_APP_ZB_ALARM_ACTION_FAILURE` alarm bit, emit an audit event, and trigger LED error feedback
- Add `sdf_power_mark_activity()` to prevent sleep during failure handling

## Capabilities

### New Capabilities
- None

### Modified Capabilities
- None (no spec-level behavior change, this restores intended behavior)

## Impact

- `firmware/components/sdf_app/src/sdf_app.c` — add case in `sdf_app_on_event()`
- Zigbee coordinator will now receive an alarm when enrollment fails
- LED will flash red on enrollment failure (consistent with other failure paths)
