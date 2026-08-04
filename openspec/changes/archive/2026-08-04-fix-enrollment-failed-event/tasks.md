## 1. Implement Handler

- [x] 1.1 In `sdf_app.c`, check `sdf_event_router.h` for the exact payload union member used by `SDF_EVENT_ROUTER_ENROLLMENT_FAILED`
- [x] 1.2 Add `case SDF_EVENT_ROUTER_ENROLLMENT_FAILED:` to the switch in `sdf_app_on_event()`:
  - Call `sdf_power_mark_activity()`
  - Log warning with user_id and result code
  - Call `sdf_app_set_alarm_mask_bits(SDF_APP_ZB_ALARM_ACTION_FAILURE, 0)`
  - Call `sdf_app_emit_audit()` with appropriate audit type and user_id
  - Call `led_flash_red()`

## 2. Verify

- [x] 2.1 Build firmware, confirm no compile errors
- [x] 2.2 Confirm `SDF_EVENT_ROUTER_ENROLLMENT_FAILED` is actually emitted by the enroll task on failure (check `sdf_services_enroll.c`)
