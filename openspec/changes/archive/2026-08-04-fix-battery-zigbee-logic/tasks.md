## 1. Fix Logic Inversion

- [x] 1.1 In `firmware/components/sdf_app/src/sdf_app.c`, change `if (err != ESP_OK && sdf_protocol_zigbee_is_enabled())` to `if (err == ESP_OK && sdf_protocol_zigbee_is_enabled())` in `sdf_app_update_battery_percent()`
- [x] 1.2 Verify no other call site also calls `sdf_protocol_zigbee_update_battery_percent()` to confirm no double-reporting

## 2. Verify & Test

- [x] 2.1 Build firmware and confirm no compilation errors
- [x] 2.2 Trace the battery reporting path: power manager periodic callback → `sdf_app_power_battery_percent` → `sdf_app_update_battery_percent` → confirm Zigbee now receives the value
