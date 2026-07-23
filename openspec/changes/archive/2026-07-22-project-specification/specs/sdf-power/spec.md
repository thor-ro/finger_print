## ADDED Requirements

### Requirement: Power Manager Task
The `sdf_power` component SHALL run a FreeRTOS power manager task that coordinates sleep/wake cycles, Zigbee check-ins, and BLE radio gating.

#### Scenario: Power task loop
- **WHEN** `sdf_power_init()` called
- **THEN** Create task `sdf_pwr` with stack `CONFIG_SDF_POWER_TASK_STACK_SIZE` (default 4KB)
- **THEN** Priority `CONFIG_SDF_POWER_TASK_PRIORITY` (default 4)
- **THEN** Task runs every `CONFIG_SDF_POWER_LOOP_INTERVAL_MS` (default 250ms)

### Requirement: Sleep State Machine
The `sdf_power` component SHALL implement a sleep state machine: SLEEP → WAKE_FINGER/WAKE_ZIGBEE → ACTIVE → BLE_ACTION → REPORT → SLEEP.

#### Scenario: Deep sleep entry
- **WHEN** No activity for `CONFIG_SDF_POWER_IDLE_BEFORE_SLEEP_MS` (default 5000ms)
- **THEN** Disable BLE radio via `sdf_protocol_ble_disable()`
- **THEN** Power off fingerprint sensor
- **THEN** Turn off LED
- **THEN** Configure wake sources: GPIO 3 (WAKE pin), Zigbee check-in timer
- **THEN** Enter deep sleep (`esp_deep_sleep_start()`)

#### Scenario: Wake on fingerprint
- **WHEN** WAKE pin interrupt (GPIO 3, falling edge)
- **THEN** Wake from deep sleep
- **THEN** State: WAKE_FINGER
- **THEN** Power on fingerprint sensor
- **THEN** Signal `sdf_services` to start match cycle
- **THEN** Guard time: stay awake `CONFIG_SDF_POWER_POST_WAKE_GUARD_MS` (default 1500ms)

#### Scenario: Wake on Zigbee check-in
- **WHEN** Check-in timer expires (every `CONFIG_SDF_POWER_CHECKIN_INTERVAL_MS`, default 15000ms)
- **THEN** Wake from deep sleep
- **THEN** State: WAKE_ZIGBEE
- **THEN** Poll Zigbee parent for messages
- **THEN** If message received: process, stay awake for guard time
- **THEN** If no message: return to sleep

#### Scenario: BLE action state
- **WHEN** Lock action or pairing initiated
- **THEN** State: BLE_ACTION
- **THEN** Enable BLE radio
- **THEN** Stay awake until action completes + keyturner sync
- **THEN** State: REPORT (send Zigbee attribute updates)
- **THEN** Return to ACTIVE, then idle timer starts

### Requirement: BLE Radio Gating
The `sdf_power` component SHALL coordinate BLE radio enable/disable with `sdf_protocol_ble` and `sdf_app`.

#### Scenario: Radio gated on demand
- **WHEN** `sdf_power_request_ble()` called
- **THEN** If radio off: call `sdf_protocol_ble_enable()`
- **THEN** Start BLE activity timer
- **WHEN** `sdf_power_release_ble()` called
- **THEN** If no other requesters: call `sdf_protocol_ble_disable()`

### Requirement: Battery Reporting
The `sdf_power` component SHALL periodically read battery and report to Zigbee and LED.

#### Scenario: Periodic battery report
- **WHEN** Every `CONFIG_SDF_POWER_BATTERY_REPORT_INTERVAL_MS` (default 60000ms)
- **THEN** Call `battery_get_percentage()`
- **THEN** Call `sdf_protocol_zigbee_update_battery(percentage)`
- **THEN** If < 20%: call `led_pulse_red_slow()`

#### Scenario: Battery on wake
- **WHEN** Wake from deep sleep
- **THEN** Read battery, if critical (< 10%): extend awake time for reporting

### Requirement: Light Sleep Support
The `sdf_power` component SHALL optionally use FreeRTOS light sleep between power loop iterations.

#### Scenario: Light sleep
- **WHEN** `CONFIG_SDF_POWER_ENABLE_LIGHT_SLEEP=1`
- **THEN** In power loop: `vTaskDelay()` replaced with light sleep
- **THEN** Wake on GPIO interrupt or timer