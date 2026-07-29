## ADDED Requirements

### Requirement: Light-sleep entry logic is extracted to a shared helper
The system SHALL extract the duplicated light-sleep entry sequence from `sdf_power_sleep_once()` and `sdf_power_task()` into a shared helper function `sdf_power_enter_light_sleep()`.

#### Scenario: Light sleep via one-shot path
- **WHEN** `sdf_power_sleep_once()` is called
- **THEN** it delegates to `sdf_power_enter_light_sleep()` for the sleep-entry sequence
- **AND** the sleep entry emits a `SDF_EVENT_ROUTER_POWER_SLEEP` event, configures timer and GPIO wake sources, gates BLE radio, invokes `sdf_platform_sleep_light()`, restores BLE, maps the wake reason, and calls the wake callback

#### Scenario: Light sleep via power task loop path
- **WHEN** `sdf_power_task()` decides to enter light sleep
- **THEN** it calls `sdf_power_enter_light_sleep()` with the current config snapshot
- **AND** the behavior matches the one-shot path exactly

#### Scenario: Light sleep disabled by config
- **WHEN** `config->enable_light_sleep` is false
- **THEN** `sdf_power_enter_light_sleep()` returns immediately without entering sleep

### Requirement: Helper function returns esp_err_t for error handling
The shared helper SHALL return `esp_err_t` to allow callers to handle sleep-entry failures.

#### Scenario: Sleep entry fails
- **WHEN** `sdf_platform_sleep_light()` returns an error
- **THEN** `sdf_power_enter_light_sleep()` returns that error code
- **AND** the caller logs a warning and continues the main loop