## Context

`sdf_app_update_battery_percent()` is called on two paths:
1. Zigbee init at boot (passes `cfg->battery_default_percent`)
2. Power manager's periodic battery callback (calls `sdf_app_power_battery_percent` → `sdf_drivers_battery_get_percent()`)

The function stores the value in the power manager via `sdf_power_set_battery_percent()` AND propagates it to the Zigbee stack. The condition `if (err != ESP_OK)` means Zigbee only gets updates when the power manager rejects the value — i.e., never in normal operation.

## Goals / Non-Goals

**Goals:**
- Fix the condition so Zigbee receives the battery percent on every successful update
- Verify no other code path also propagates battery to Zigbee (avoid double-reporting)

**Non-Goals:**
- Changing battery sampling frequency
- Changing Zigbee battery attribute format

## Decisions

**Fix: invert the condition.**

```c
// Before (broken):
esp_err_t err = sdf_power_set_battery_percent(battery_percent);
if (err != ESP_OK && sdf_protocol_zigbee_is_enabled()) {
    sdf_protocol_zigbee_update_battery_percent(battery_percent);
}

// After (correct):
esp_err_t err = sdf_power_set_battery_percent(battery_percent);
if (err == ESP_OK && sdf_protocol_zigbee_is_enabled()) {
    sdf_protocol_zigbee_update_battery_percent(battery_percent);
}
```

Alternative considered: move the Zigbee update inside `sdf_power_set_battery_percent()`. Rejected — the power module should not have a dependency on the Zigbee protocol module; the app layer is the right place to fanout.

## Risks / Trade-offs

- [Double-report risk] The power manager's `sdf_power_push_battery_percent()` also exists internally — verify it does not already call Zigbee. Searching the codebase confirms `sdf_power_push_battery_percent` reports only to the power module's own internal state, not Zigbee. No double-report.
- [Out-of-range values] `sdf_power_set_battery_percent` validates 0–100 and returns `ESP_ERR_INVALID_ARG` for values >100. The fix correctly gates Zigbee update on `ESP_OK`, so invalid values are still rejected.
