# Fix Zigbee commissioning state and stop the idle watchdog reboot loop

## Why

Once the BLE bond-seed panic (`fix-ble-bond-seed-init-order`) was fixed, the
device booted far enough to expose three defects that had been masked behind it.
All three were confirmed on the connected esp32c6 over USB.

### 1. A botched edit collapsed the commissioning if/else

`sdf_protocol_zigbee.c:125-142` had lost its `else` branch. The reboot-onto-an-
existing-network body had been folded into the factory-new branch, with one
statement left at column 0 — the shape of an edit that dropped a line:

```c
if (esp_zb_bdb_is_factory_new()) {
  ESP_LOGI(TAG, "Start network steering");
  esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
  ESP_LOGI(TAG, "Device rebooted and using existing network");
esp_zb_ota_upgrade_client_query_interval_set(SDF_ZIGBEE_ENDPOINT, ...);
}
```

Two consequences, in order of severity:

- **A commissioned lock never becomes ready again after a reboot.**
  `sdf_zigbee_set_network_joined(true)` lived in the lost `else`. On the
  non-factory-new path no steering runs, so `ESP_ZB_BDB_SIGNAL_STEERING` never
  fires either — that `else` was the *only* place the rejoined state got
  recorded. `s_state.network_joined` therefore stays `false` for the rest of the
  boot, and `sdf_protocol_zigbee_is_ready()` (`ready = stack_started &&
  network_joined`) stays false, so no lock state is ever reported upstream. A
  lock that has been commissioned into a network goes silent after any reboot.
- **OTA query interval set on the wrong path**, and the device logs both
  "Start network steering" and "Device rebooted and using existing network"
  together — observed verbatim on hardware.

The adjacent format string `"Device started in %s factory-reset mode"` with
`? "" : "non"` also prints a double space in the factory-new case.

### 2. The idle task watchdog panics during a radio scan

At ~18 s the device aborted and rebooted, continuously:

```
E (18007) task_wdt: - IDLE (CPU 0)
Tasks currently running: CPU 0: sdf_zigbee
```

symbolizing into `zb_mac_logic_iteration` in the closed-source
`esp-zigbee-lib`. An all-channel active scan runs on the `sdf_zigbee` task at
priority 5; on this unicore part that starves the priority-0 idle task for the
duration. Measured on hardware: a continuous 15 s stretch, from ~3 s to ~18 s.
`sdf_app.c:1640` watches the idle tasks (`idle_core_mask = (1 <<
portNUM_PROCESSORS) - 1`) with `trigger_panic = true`, so the scan reboots the
device — while every task the firmware actually owns was healthy and feeding the
watchdog on time.

### 3. Steering retries at a flat 1 s forever

`sdf_protocol_zigbee.c:174` rescheduled failed steering after 1000 ms with no
backoff and no cap. Each attempt is a full all-channel active scan — the most
expensive thing the radio does. On a battery-powered lock that is out of range
of its coordinator, that is an unbounded drain.

## What Changes

- Restore the `else` branch in the `DEVICE_FIRST_START`/`DEVICE_REBOOT` handler,
  including `sdf_zigbee_set_network_joined(true)` and the OTA query interval
  call, and fix the double-space format string.
- Set `idle_core_mask = 0` in `sdf_app_init()`'s TWDT config, keeping
  `trigger_panic = true`. This narrows the watchdog to the tasks that explicitly
  subscribe via `sdf_platform_time_wdt_add()`, so a genuinely wedged service
  task still panics and reboots — as `sdf-services-tasks` requires — while a
  busy radio scan no longer does.
- Replace the flat steering retry with geometric backoff from 1 s to a 60 s
  ceiling, reset on a successful join and when steering is started fresh.

## Impact

- Affected specs: `sdf-services-tasks` (MODIFIED — scope the watchdog to
  explicit subscribers), `zigbee-commissioning` (ADDED).
- Affected code: `firmware/components/sdf_protocol_zigbee/src/sdf_protocol_zigbee.c`,
  `firmware/components/sdf_app/src/sdf_app.c`.
- Behavioral: a rebooted, already-commissioned lock now reports its state again;
  the device no longer reboots every ~18 s; an unreachable network costs one
  scan per minute instead of sixty.
- Not addressed: the underlying idle starvation inside `esp-zigbee-lib`. The
  stack is closed source, so it cannot be fixed at the source. Dropping the idle
  cores from the watchdog is a scope decision about what the watchdog is *for*,
  not a workaround for a bug in our own code — see design.md.
