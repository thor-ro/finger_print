# SDF Runtime Config

## Purpose
Defines the contract that a runtime-configurable field in `sdf_config_t` is actually read by the subsystem it names — covering per-field wiring (BLE nonce replay window, task watchdog timeout, adaptive check-in scaling and scheduling, Zigbee enablement, deep-sleep gating), the layering rule for consumer-side clamping, the rule that a field whose consumer initializes before `sdf_config_init()` does not belong in `sdf_config_t`, the removal of dead/duplicate fields, and struct/NVS compatibility behavior on layout change.

## Requirements

### Requirement: Runtime config fields are read by the subsystem they name
Every field in `sdf_config_t` SHALL be read from the live configuration by the subsystem whose behavior it describes, at the point where that behavior is decided. A subsystem SHALL NOT use a compile-time constant, macro, or literal in place of a field that exists in `sdf_config_t`.

#### Scenario: A configured field governs subsystem behavior
- **WHEN** a field in `sdf_config_t` holds a value that differs from its Kconfig default
- **THEN** the subsystem named by that field behaves according to the field's current value, not the Kconfig default

#### Scenario: No shadow constant governs a configured field
- **WHEN** the firmware sources are inspected for a subsystem that consumes an `sdf_config_t` field
- **THEN** that subsystem contains no macro or literal that independently determines the same behavior

### Requirement: BLE nonce replay window is sourced from runtime configuration
`sdf_protocol_ble` SHALL determine the number of recently accepted nonces it tracks per client from `sdf_config_get()->nonce_replay_window`, read at the point of use rather than captured at compile time.

#### Scenario: Replay window follows the configured value
- **WHEN** `nonce_replay_window` is set to `N` where `1 <= N <= SDF_NUKI_NONCE_CACHE_MAX` and a client receives more than `N` distinct encrypted messages
- **THEN** the client retains exactly the `N` most recently accepted nonces for replay comparison

#### Scenario: A replayed nonce within the window is rejected
- **WHEN** a client receives an encrypted message bearing a nonce that is still within the configured replay window for that authorization ID
- **THEN** the message is rejected as a replay

#### Scenario: A nonce evicted from the window is no longer matched
- **WHEN** more than `nonce_replay_window` distinct nonces have been accepted since a given nonce was recorded
- **THEN** that nonce is no longer present in the client's replay cache

### Requirement: Nonce replay window is clamped at the consumer
`sdf_protocol_ble` SHALL clamp the configured `nonce_replay_window` to `SDF_NUKI_NONCE_CACHE_MAX` at runtime before using it to index the nonce cache, so that a configuration value exceeding the statically sized cache cannot cause an out-of-bounds access. `sdf_config_validate()` SHALL NOT encode `SDF_NUKI_NONCE_CACHE_MAX`, which is owned by `sdf_protocol_ble`.

#### Scenario: Oversized window is clamped, not honored
- **WHEN** `nonce_replay_window` exceeds `SDF_NUKI_NONCE_CACHE_MAX`
- **THEN** the effective replay window is `SDF_NUKI_NONCE_CACHE_MAX` and no read or write occurs beyond the bounds of `rx_nonce_cache`

#### Scenario: Zero window disables replay tracking without indexing
- **WHEN** the effective replay window is `0`
- **THEN** no nonce is recorded and the nonce cache is not indexed

### Requirement: Task watchdog timeout is sourced from runtime configuration
`sdf_app` SHALL configure the ESP task watchdog using `sdf_config_get()->wdt_timeout_ms` when it calls `esp_task_wdt_reconfigure()`, rather than a hardcoded timeout literal.

#### Scenario: Watchdog is armed with the configured timeout
- **WHEN** `sdf_app` reconfigures the task watchdog during startup
- **THEN** the `timeout_ms` field of `esp_task_wdt_config_t` equals the current `wdt_timeout_ms` from the live configuration

#### Scenario: Configuration is read after config init
- **WHEN** the task watchdog is reconfigured
- **THEN** `sdf_config_init()` has already run, so a persisted `wdt_timeout_ms` override is in effect rather than the Kconfig default

### Requirement: Adaptive check-in scaling is gated by its configuration flag
`sdf_power_calculate_checkin_interval()` SHALL return the unscaled `checkin_interval_ms` when `adaptive_checkin` is false, and SHALL apply battery-level scaling to it only when `adaptive_checkin` is true.

#### Scenario: Scaling is suppressed when disabled
- **WHEN** `adaptive_checkin` is false and the battery level is below 20 percent
- **THEN** `sdf_power_calculate_checkin_interval()` returns exactly `checkin_interval_ms`

#### Scenario: Scaling applies when enabled and battery is low
- **WHEN** `adaptive_checkin` is true and the battery level is below 20 percent
- **THEN** `sdf_power_calculate_checkin_interval()` returns a value greater than `checkin_interval_ms`

#### Scenario: Scaling is inert at full battery even when enabled
- **WHEN** `adaptive_checkin` is true and the battery level is at or above 60 percent
- **THEN** `sdf_power_calculate_checkin_interval()` returns exactly `checkin_interval_ms`

### Requirement: Check-in scheduling uses the adaptive interval
`sdf_power` SHALL obtain every check-in interval it schedules or propagates from `sdf_power_calculate_checkin_interval()`, so that enabling `adaptive_checkin` takes effect on all check-in timing rather than a subset of it.

#### Scenario: Light-sleep wake timer uses the adaptive interval
- **WHEN** `sdf_power` arms the light-sleep timer wakeup
- **THEN** the timer duration is the value returned by `sdf_power_calculate_checkin_interval()`

#### Scenario: Sleep event reports the interval the timer was armed with
- **WHEN** `sdf_power` emits a `SDF_EVENT_ROUTER_POWER_SLEEP` event before entering light sleep
- **THEN** the event's `remaining_ms` equals the interval used to arm the light-sleep timer

#### Scenario: Deep-sleep wake timer uses the adaptive interval
- **WHEN** `sdf_power` arms the deep-sleep timer wakeup
- **THEN** the timer duration is the value returned by `sdf_power_calculate_checkin_interval()`

#### Scenario: Retention state records the adaptive interval
- **WHEN** `sdf_power` writes `next_checkin_us` into retention state before deep sleep
- **THEN** the interval used is the value returned by `sdf_power_calculate_checkin_interval()`

#### Scenario: Zigbee check-in propagation uses the adaptive interval
- **WHEN** `sdf_power` propagates the check-in interval to Zigbee
- **THEN** the value propagated is the value returned by `sdf_power_calculate_checkin_interval()`

### Requirement: Adaptive check-in ships disabled by default
`CONFIG_SDF_POWER_ADAPTIVE_CHECKIN` SHALL default to disabled, so that connecting the adaptive check-in implementation does not alter check-in timing on an existing deployment without an explicit configuration decision.

#### Scenario: Default build preserves fixed check-in timing
- **WHEN** the firmware is built with Kconfig defaults
- **THEN** `adaptive_checkin` is false and check-in intervals are unaffected by battery level

### Requirement: Zigbee enablement is sourced from runtime configuration
`sdf_protocol_zigbee_is_enabled()` SHALL return `sdf_config_get()->zigbee_enabled` rather than a compile-time macro derived from `CONFIG_SDF_ZIGBEE_ENABLE`, so that `sdf_config_set_zigbee_enabled()` takes effect on the subsystem it names.

#### Scenario: Build-time disabled Zigbee remains disabled
- **WHEN** the firmware is built with `CONFIG_SDF_ZIGBEE_ENABLE` unset
- **THEN** `zigbee_enabled` is false after `sdf_config_init()` and `sdf_protocol_zigbee_is_enabled()` returns false

#### Scenario: Build-time enabled Zigbee is enabled by default
- **WHEN** the firmware is built with `CONFIG_SDF_ZIGBEE_ENABLE` set and no persisted override applies
- **THEN** `sdf_protocol_zigbee_is_enabled()` returns true after `sdf_config_init()`

#### Scenario: Runtime kill switch takes effect
- **WHEN** `sdf_config_set_zigbee_enabled(false)` is called on a build where Zigbee is enabled
- **THEN** `sdf_protocol_zigbee_is_enabled()` returns false

### Requirement: Deep sleep is gated by a single configured flag
The deep-sleep entry decision SHALL be governed by exactly one field in `sdf_config_t`, `enable_deep_sleep_fallback`, sourced from `CONFIG_SDF_POWER_ENABLE_DEEP_SLEEP` rather than a hardcoded literal. The redundant `enable_deep_sleep` field SHALL be removed.

#### Scenario: Deep sleep gate follows the Kconfig symbol
- **WHEN** `CONFIG_SDF_POWER_ENABLE_DEEP_SLEEP` is set
- **THEN** `enable_deep_sleep_fallback` is true after `sdf_config_init()`

#### Scenario: Disabling deep sleep at build time is honored
- **WHEN** `CONFIG_SDF_POWER_ENABLE_DEEP_SLEEP` is unset
- **THEN** `enable_deep_sleep_fallback` is false and `sdf_power_policy_evaluate()` never returns `SDF_POWER_POLICY_DECISION_SLEEP_DEEP`

#### Scenario: Only one deep-sleep flag exists
- **WHEN** `sdf_config_t` is inspected
- **THEN** no `enable_deep_sleep` field is present

### Requirement: A boolean field assigned from Kconfig compiles in both symbol states
Any assignment in `sdf_config_get_defaults()` that reads a Kconfig `bool` symbol SHALL be guarded with `#if defined(...)` and supply an explicit `false` in the `#else` branch, because Kconfig omits the macro entirely when the symbol is disabled rather than defining it to `0`.

#### Scenario: Build succeeds with the symbol disabled
- **WHEN** the firmware is built with a Kconfig `bool` symbol consumed by `sdf_config_get_defaults()` set to `n`
- **THEN** compilation succeeds and the corresponding field is false

### Requirement: Configuration does not expose unimplemented capabilities
`sdf_config_t` SHALL NOT contain a field that no subsystem reads. The `staged_wake` and `nuki_state_poll_interval_ms` fields, their Kconfig entries, their default assignments, and the `sdf_config_set_nuki_state_poll_interval()` setter SHALL be removed, as neither a staged wake sequence nor an independent Nuki state poll cadence is implemented.

#### Scenario: Removed field is absent from the configuration surface
- **WHEN** `sdf_config_t`, `sdf_config_get_defaults()`, `sdf_config_validate()`, and `sdf_config_dump()` are inspected
- **THEN** no `staged_wake` or `nuki_state_poll_interval_ms` field, assignment, validation, or log output is present

#### Scenario: Removed setter is absent from the public header
- **WHEN** `sdf_config.h` is inspected
- **THEN** `sdf_config_set_nuki_state_poll_interval()` is not declared

#### Scenario: Removed Kconfig symbols are absent
- **WHEN** the Kconfig tree is inspected
- **THEN** neither `SDF_POWER_STAGED_WAKE` nor `SDF_POWER_NUKI_STATE_POLL_INTERVAL_MS` is defined

### Requirement: Configuration does not duplicate a field it already carries
`sdf_config_t` SHALL NOT contain two fields populated from the same Kconfig symbol where only one is read. The `fp_power_en_pin` field SHALL be removed, as `fp_en_gpio` carries `CONFIG_SDF_POWER_FP_EN_GPIO` and is the field consumed by `sdf_services`.

#### Scenario: Fingerprint enable GPIO has one representation
- **WHEN** `sdf_config_t` is inspected
- **THEN** `fp_en_gpio` is present, `fp_power_en_pin` is absent, and the fingerprint power-enable consumer reads `fp_en_gpio`

### Requirement: Configuration does not carry policy its consumer needs before config init
`sdf_config_t` SHALL NOT contain a field whose consumer is initialized before `sdf_config_init()` runs, because such a field can only ever be read as a zero-initialized value. The `require_encrypted_nvs` field SHALL be removed; `sdf_storage` remains the sole owner of the encrypted-NVS policy, exposed through `sdf_storage_get_security_status()`.

#### Scenario: Encrypted-NVS policy has one source of truth
- **WHEN** the encrypted-NVS requirement is consulted anywhere in the firmware
- **THEN** the value read originates from `sdf_storage_security_status_t`, and no `require_encrypted_nvs` field exists in `sdf_config_t`

#### Scenario: Storage policy is available before config init
- **WHEN** `sdf_storage_init()` validates the security policy at startup
- **THEN** it does so without reading `sdf_config_get()`, which has not yet been initialized

### Requirement: Every field in the runtime configuration has a live reader
A field SHALL NOT remain in `sdf_config_t` unless at least one subsystem outside the `sdf_config` component reads it. Adding a field without a reader, or removing the last reader of an existing field, SHALL be treated as a defect.

#### Scenario: Field-by-field sweep finds no dead field
- **WHEN** every field of `sdf_config_t` is searched for across the firmware sources, excluding the `sdf_config` component itself
- **THEN** each field has at least one read outside `sdf_config`

### Requirement: Persisted configuration is rejected when the struct layout changes
`sdf_config_load_persisted()` SHALL reject a persisted blob whose size does not match `sizeof(sdf_config_t)`, warn that the persisted overrides were dropped, and leave the caller on Kconfig defaults.

#### Scenario: Blob saved before field removal is discarded
- **WHEN** a persisted configuration blob written before these fields were removed is read back after the upgrade
- **THEN** the size mismatch is detected, a warning is logged, and the runtime configuration falls back to Kconfig defaults

#### Scenario: Matching blob is accepted
- **WHEN** a persisted blob matches the current `sizeof(sdf_config_t)` and passes validation
- **THEN** it is applied as the runtime configuration
