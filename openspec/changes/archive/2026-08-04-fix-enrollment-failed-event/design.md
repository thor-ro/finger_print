## Context

The ENROLLMENT_FAILED event is subscribed but not handled. All other terminal enrollment states (STEP_COMPLETE, COMPLETE) have handlers. The gap means users can't see enrollment failures reflected in Zigbee alarms or LED.

## Goals / Non-Goals

**Goals:**
- Handle enrollment failures with appropriate alarm, audit, and LED feedback
- Be consistent with how other error paths in the app work

**Non-Goals:**
- Changing enrollment logic or retry behavior

## Decisions

**Mirror the pattern from other failure handlers:**

```c
case SDF_EVENT_ROUTER_ENROLLMENT_FAILED: {
    sdf_power_mark_activity();
    ESP_LOGW(TAG, "Event: Enrollment FAILED user_id=%u result=%u",
             (unsigned)event->payload.enrollment_complete.user_id,
             (unsigned)event->payload.enrollment_complete.result);
    sdf_app_set_alarm_mask_bits(SDF_APP_ZB_ALARM_ACTION_FAILURE, 0);
    sdf_app_emit_audit(SDF_AUDIT_BIOMETRIC_FAILED, 
                       event->payload.enrollment_complete.user_id,
                       (int32_t)event->payload.enrollment_complete.result, 0);
    led_flash_red();
    break;
}
```

Need to verify `event->payload.enrollment_complete` is the correct payload union member for ENROLLMENT_FAILED (check `sdf_event_router.h`). Adjust if it uses a different payload field.

## Risks / Trade-offs

- [Alarm persistence] `SDF_APP_ZB_ALARM_ACTION_FAILURE` is set but only cleared on the next successful enrollment or lock action. This is consistent with other failure paths.
