## Why

`sdf_app_update_battery_percent()` has an inverted condition: it only propagates the battery level to the Zigbee stack when `sdf_power_set_battery_percent()` **fails**, meaning Zigbee never receives battery updates during normal operation. This is a silent bug — the Zigbee coordinator sees a stale battery percentage from boot until first failure.

## What Changes

- Invert the condition in `sdf_app_update_battery_percent()` from `err != ESP_OK` to `err == ESP_OK`
- Confirm the battery reporting path is exercised by the power manager's periodic callback
- Add a test to verify the battery propagation logic

## Capabilities

### New Capabilities
- None

### Modified Capabilities
- None (this is a one-line logic fix with no spec-level behavior change)

## Impact

- `firmware/components/sdf_app/src/sdf_app.c` — one-line fix at L350–353
- No API changes, no dependency changes
- Zigbee battery attribute will now be correctly updated on every battery percent report cycle
