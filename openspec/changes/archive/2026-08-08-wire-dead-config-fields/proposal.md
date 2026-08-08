## Why

Nine fields in `sdf_config_t` are populated from Kconfig, validated, logged in `sdf_config_dump()`, and persisted to NVS — but never read by the code whose behavior they claim to control. The worst case is `nonce_replay_window`: an operator can set it via Kconfig, override it at runtime, persist it, watch it echoed at boot ("nonce_window=8"), and reasonably believe it governs BLE replay protection — while the actual replay window is fixed at compile time by a macro in `sdf_protocol_ble.c`. A security-relevant knob that silently does nothing is worse than no knob at all.

The same shape affects `wdt_timeout_ms` and `zigbee_enabled` (shadowed by a hardcoded literal and a macro) and `adaptive_checkin` (whose implementing function exists, works, and is called by nobody). `staged_wake` and `nuki_state_poll_interval_ms` gate features that were never built. `fp_power_en_pin` duplicates the live `fp_en_gpio` from the same Kconfig symbol. `enable_deep_sleep` is dead while the field that actually gates deep sleep, `enable_deep_sleep_fallback`, is hardcoded `true` and reads no Kconfig at all — the knob and the behavior are wired backwards. `require_encrypted_nvs` cannot be wired at all: `sdf_storage_init()` consumes the policy at `sdf_app.c:1477`, before `sdf_config_init()` at `:1489` can exist to supply it.

The first four fields were found by inspection; the remaining five by a field-by-field sweep of `sdf_config_t` (task 6.5) once the first four were fixed. They are folded in here rather than deferred so the sweep's result — *every* field in `sdf_config_t` has a live reader — becomes an enforceable invariant instead of a snapshot that decays again.

## What Changes

- **`nonce_replay_window` — wire to consumer.** `sdf_protocol_ble.c` reads `sdf_config_get()->nonce_replay_window` at the point of use instead of the compile-time `SDF_NUKI_NONCE_REPLAY_WINDOW` macro. The existing clamp to `SDF_NUKI_NONCE_CACHE_MAX` (16) stays in `sdf_protocol_ble.c`, applied at runtime. No default behavior change — Kconfig default is already `8`.
- **`wdt_timeout_ms` — wire to consumer.** `sdf_app.c` reads `sdf_config_get()->wdt_timeout_ms` when building `esp_task_wdt_config_t` instead of the hardcoded `SDF_APP_TWDT_TIMEOUT_MS` (`15000u`). No default behavior change — Kconfig default is already `15000`.
- **`adaptive_checkin` — wire to consumer, shipped disabled.** The already-implemented `sdf_power_calculate_checkin_interval()` gains an `adaptive_checkin` guard (returns the unscaled base interval when the flag is false) and is called at the four sites that currently use the raw `checkin_interval_ms`. `CONFIG_SDF_POWER_ADAPTIVE_CHECKIN` flips from `default y` to `default n` so the wiring lands behaviorally inert on a door lock; enabling it becomes a deliberate, separately-verifiable act.
- **`zigbee_enabled` — wire to consumer.** `sdf_protocol_zigbee_is_enabled()` returns `sdf_config_get()->zigbee_enabled` instead of the `SDF_ZIGBEE_ENABLED` macro it derives from `CONFIG_SDF_ZIGBEE_ENABLE`. No default behavior change — `sdf_config_get_defaults()` already populates the field from the same symbol. This also makes the existing public setter `sdf_config_set_zigbee_enabled()` real for the first time.
- **`enable_deep_sleep` — collapsed into `enable_deep_sleep_fallback`.** The dead field is removed and the live one is sourced from `CONFIG_SDF_POWER_ENABLE_DEEP_SLEEP` instead of the hardcoded `true`. Deep-sleep-fallback *is* the only deep-sleep entry path in `sdf_power_policy`, so the two named the same decision. No default behavior change — the symbol is `y`, matching the hardcode.
- **`staged_wake` — removed.** The field, its Kconfig entry, and its `sdf_config_get_defaults()` assignment are deleted. Nothing implements "critical → fast path (BLE) → full path (Zigbee)" staging, and building it requires design work out of scope here (see Impact). Removing the field stops it from advertising a capability that does not exist.
- **`nuki_state_poll_interval_ms` — removed.** Same shape as `staged_wake`: no reader, no shadow constant, no implementation. Nuki state refresh is currently driven by the power check-in wake, not a dedicated cadence. The field, its Kconfig entry, and its `sdf_config_set_nuki_state_poll_interval()` setter are deleted.
- **`fp_power_en_pin` — removed as a duplicate.** It is populated from `CONFIG_SDF_POWER_FP_EN_GPIO` (`sdf_config.c:44`), the exact same symbol that populates the live `fp_en_gpio` (`:73`). Two fields, one source, one consumer. The dead one goes.
- **`require_encrypted_nvs` — removed as structurally unwireable.** `sdf_storage` owns this policy in `sdf_storage_security_status_t`, populated from `CONFIG_SDF_SECURITY_REQUIRE_ENCRYPTED_NVS` and queried at `sdf_app.c:1511`. The `sdf_config_t` copy cannot become authoritative because `sdf_storage_init()` runs at `sdf_app.c:1477` and `sdf_config_init()` at `:1489` — config init depends on storage having brought up NVS first, so the policy is needed strictly before the config that would carry it exists.
- **Regression guard.** Host tests assert each wired field actually reaches its consumer, so these cannot silently decay back into shadowed constants.

**BREAKING** (config surface, not API): `staged_wake`, `nuki_state_poll_interval_ms`, `fp_power_en_pin`, `enable_deep_sleep`, and `require_encrypted_nvs` are removed from `sdf_config_t`, and `sdf_config_set_nuki_state_poll_interval()` is removed from the public header. This changes the struct layout, so `sdf_config_load_persisted()` will reject previously-saved NVS blobs on size mismatch and fall back to Kconfig defaults. That path already exists, already warns, and is the documented behavior for struct-layout changes (`sdf_config.c:114-121`) — but any persisted runtime overrides are dropped on first boot after upgrade. None of the removed fields are exposed through the BLE companion config surface (`sdf_ble_companion.c:339-349`, `sdf_app.c:534-573`), so no companion-visible knob disappears.

## Capabilities

### New Capabilities
- `sdf-runtime-config`: Defines the contract that a runtime-configurable field in `sdf_config_t` is actually read by the subsystem it names — covering the nine fields above, the layering rule for consumer-side clamping, the rule that a field whose consumer initializes before `sdf_config_init()` does not belong in `sdf_config_t`, and the struct/NVS compatibility behavior on layout change.

### Modified Capabilities
<!-- None. No existing spec in openspec/specs/ covers sdf_config, sdf_power, or the BLE nonce path at requirement level. -->

## Impact

**Code:**
- `firmware/components/sdf_config/include/sdf_config.h` — remove `staged_wake`, `nuki_state_poll_interval_ms`, `fp_power_en_pin`, `enable_deep_sleep`, `require_encrypted_nvs` and the `sdf_config_set_nuki_state_poll_interval()` declaration
- `firmware/components/sdf_config/src/sdf_config.c` — remove the removed fields' defaults, dump lines, validation, and the `sdf_config_set_nuki_state_poll_interval()` definition; source `enable_deep_sleep_fallback` from `CONFIG_SDF_POWER_ENABLE_DEEP_SLEEP`
- `firmware/components/sdf_config/Kconfig` — remove `SDF_POWER_STAGED_WAKE` and `SDF_POWER_NUKI_STATE_POLL_INTERVAL_MS`; flip `SDF_POWER_ADAPTIVE_CHECKIN` to `default n`
- `firmware/components/sdf_protocol_zigbee/src/sdf_protocol_zigbee.c` — `sdf_protocol_zigbee_is_enabled()` reads config; drop the `SDF_ZIGBEE_ENABLED` macro
- `firmware/components/sdf_protocol_ble/src/sdf_protocol_ble.c` — replace macro with runtime read + clamp in `sdf_nuki_nonce_remember()` and the replay-check path
- `firmware/components/sdf_protocol_ble/CMakeLists.txt` — add `sdf_config` to `PRIV_REQUIRES`
- `firmware/components/sdf_app/src/sdf_app.c` — drop `SDF_APP_TWDT_TIMEOUT_MS`, read config
- `firmware/components/sdf_power/src/sdf_power.c` — guard `sdf_power_calculate_checkin_interval()`; call it at `:161`, `:169`, `:317`, `:322`, `:425`, `:651`
- `firmware/sdkconfig`, `firmware/test_runner/sdkconfig` — clear the stale explicit `CONFIG_SDF_POWER_ADAPTIVE_CHECKIN=y` so the new Kconfig `default n` takes effect

**Dependencies:** `sdf_protocol_ble` gains a private dependency on `sdf_config`. Verified no cycle — `sdf_config`'s own requirements are `sdf_platform` and `sdf_protocol_zigbee`, neither of which reaches `sdf_protocol_ble`; the same private-dependency shape already exists for `sdf_platform` and `sdf_protocol_zigbee`. `sdf_protocol_zigbee` needs no new dependency: it already carries `sdf_config` in `PRIV_REQUIRES`.

**Behavior:** No change on default builds. `nonce_replay_window`, `wdt_timeout_ms`, `zigbee_enabled`, and the deep-sleep gate keep their current effective values; `adaptive_checkin` ships off, preserving today's fixed check-in interval.

**Ordering constraint (new, load-bearing):** wiring `zigbee_enabled` means `sdf_protocol_zigbee_is_enabled()` returns `false` before `sdf_config_init()` runs, where the macro returned `true` unconditionally. All 15 call sites were audited and every one is in a runtime path or in `sdf_app` init after `sdf_config_init()` (`:1489`); `sdf_protocol_zigbee_init()` itself runs at `:1723`. The `linux` host build keeps its independent `sdf_protocol_zigbee_mock_linux.c` implementation and is unaffected.

**Out of scope:** Implementing a dedicated Nuki state poll cadence. Today Nuki state refresh piggybacks on the power check-in wake, which is coherent for a battery device; giving it an independent timer is a power/freshness trade-off that needs its own justification, not a side effect of removing a dead field. Also out of scope: moving the NVS-encryption policy into `sdf_config_t` by restructuring boot order so config loads before storage. That inverts a deliberate dependency (config persistence lives in NVS) and would need a two-phase config init.

**Out of scope:** Implementing staged wake. That needs the wake-reason plumbing fixed first — `sdf_power.c:307` hardcodes `WAKE_REASON_TIMER` for every light-sleep wake and `sdf_power_policy_handle_wake()` discards its `reason` argument, so the policy layer cannot stage on wake cause today. It also needs a Zigbee-rejoin gating design and a decision on the duplicated `sdf_power_wake_reason_t` / `sdf_power_policy_wake_reason_t` enums. Worth its own proposal.
