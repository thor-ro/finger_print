# Power Management

The smart door lock firmware uses an active power management strategy to minimize power consumption while ensuring the lock remains responsive to Zigbee commands, BLE connections, and fingerprint interactions. The power management logic is centralized in the `sdf_power` FreeRTOS task defined in `sdf_tasks.c`.

## Core Logic and Sleep Gating

The power manager evaluates whether the system can safely enter light sleep at each execution of its scheduler loop (default 250ms interval). Sleep is only permitted if all of the following conditions are met:

1. **Light Sleep Enabled:** The `CONFIG_SDF_POWER_ENABLE_LIGHT_SLEEP` flag must be set (default true).
2. **Post-Wake Guard:** A minimum guard time (`CONFIG_SDF_POWER_POST_WAKE_GUARD_MS`, default 1500ms) has elapsed since the device last woke up. This prevents rapid toggling between sleep and wake states.
3. **Idle Window:** A minimum idle time (`CONFIG_SDF_POWER_IDLE_BEFORE_SLEEP_MS`, default 5000ms) has elapsed since the last registered activity. Components can call `sdf_tasks_mark_activity()` to reset this timer.
4. **Not Busy:** The system is not actively processing a long-running operation. This is determined via a `busy_cb` callback, allowing other components to veto sleep.

## Light Sleep Operation

When all conditions are met, the power manager initiates a light sleep cycle:

1. **Wake Source Configuration:** 
   - A timer is configured to wake the device at the next Zigbee check-in interval (`CONFIG_SDF_POWER_CHECKIN_INTERVAL_MS`).
   - If configured, the fingerprint sensor's wake GPIO (`CONFIG_SDF_POWER_FP_WAKE_GPIO`) is armed to wake the processor on a level change.
3. **Fingerprint Power Control:** If the `sdf_services_task` resolves that there is no imminent biometric matching or enrollment action, it will drive the fingerprint sensor's RST line (`CONFIG_SDF_POWER_FP_EN_GPIO`) LOW to shut down power to the external UART module.
4. **BLE Radio Gating:** If `CONFIG_SDF_POWER_ENABLE_BLE_RADIO_GATING` is active, the NimBLE transport is temporarily paused.
5. **Sleep Entry:** The processor invokes `esp_light_sleep_start()`. In light sleep, the CPU is paused, most RAM is retained, and power consumption drops significantly.
6. **Wake Routine:** Upon waking:
   - The BLE radio is re-enabled.
   - The wake reason is logged and a `wake_cb` is dispatched.
   - The fingerprint power enable pin is driven HIGH to turn on the sensor array, and a `200ms` boot-delay is enforced before initiating any UART commands.

## Battery Reporting

The power task also handles periodic battery level reporting independent of the sleep state. At intervals defined by `CONFIG_SDF_POWER_BATTERY_REPORT_INTERVAL_MS` (default 60s), the task invokes a `battery_cb` to retrieve the current percentage and publishes it to the Zigbee coordinator via `sdf_protocol_zigbee_update_battery_percent`.

## Diagram

A state machine describing this logic is available in `powermanagement.puml`.
