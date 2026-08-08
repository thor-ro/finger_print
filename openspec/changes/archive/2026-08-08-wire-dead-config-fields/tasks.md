## 1. Wire `nonce_replay_window` to the BLE consumer

- [x] 1.1 Add `sdf_config` to `PRIV_REQUIRES` in `firmware/components/sdf_protocol_ble/CMakeLists.txt` and include `sdf_config.h` in `sdf_protocol_ble.c`
- [x] 1.2 Add a static helper in `sdf_protocol_ble.c` that returns the effective replay window: read `sdf_config_get()->nonce_replay_window`, clamp to `SDF_NUKI_NONCE_CACHE_MAX`, return the clamped value
- [x] 1.3 Delete the `SDF_NUKI_NONCE_REPLAY_WINDOW` macro block (`sdf_protocol_ble.c:25-29`) including its compile-time clamp
- [x] 1.4 Replace all four macro uses in `sdf_nuki_nonce_remember()` (`:239`, `:244`, `:251`, `:252`) with the helper, reading it once into a local at function entry
- [x] 1.5 Ensure the zero-window case short-circuits before any indexing or modulo (a `% 0` would fault) — verify the existing `== 0` early return at `:239` uses the helper value
- [x] 1.6 Audit the replay-check path (`:227-229`) for any bound that must also follow the effective window rather than `rx_nonce_cache_count` — no change needed: `rx_nonce_cache_count` is only ever incremented in `sdf_nuki_nonce_remember()` while `< window`, so it is structurally bounded by whatever window was in effect at each remember() call and never exceeds `SDF_NUKI_NONCE_CACHE_MAX`; using it as the `sdf_nuki_nonce_seen()` loop bound cannot read out of bounds
- [x] 1.7 Remove the now-unused `CONFIG_SDF_SECURITY_NONCE_REPLAY_WINDOW=8` compile definitions from `firmware/test_runner/main/CMakeLists.txt` (both the `${COMPONENT_LIB}` and `__idf_sdf_protocol_ble` blocks)

## 2. Wire `wdt_timeout_ms` to the app consumer

- [x] 2.1 Replace `.timeout_ms = SDF_APP_TWDT_TIMEOUT_MS` at `sdf_app.c:1500` with a read of `sdf_config_get()->wdt_timeout_ms`
- [x] 2.2 Delete the `SDF_APP_TWDT_TIMEOUT_MS` definition (`sdf_app.c:46`)
- [x] 2.3 Confirm `sdf_config_init()` runs before the `esp_task_wdt_reconfigure()` call site so a persisted override is in effect, not the zero-initialized struct — confirmed: `sdf_config_init()` runs at `sdf_app.c:1491`, `esp_task_wdt_reconfigure()` at `:1504`

## 3. Wire `adaptive_checkin` and ship it disabled

- [x] 3.1 Add an `adaptive_checkin` guard at the top of `sdf_power_calculate_checkin_interval()` (`sdf_power.c:691`) returning the unscaled `checkin_interval_ms` when the flag is false
- [x] 3.2 Replace the raw `checkin_interval_ms` read at `sdf_power.c:317` (deep-sleep timer wakeup) with a call to `sdf_power_calculate_checkin_interval()`
- [x] 3.3 Replace the raw read at `sdf_power.c:322-323` (`retention.next_checkin_us`) with the same call
- [x] 3.4 Replace the raw read at `sdf_power.c:425` (Zigbee check-in propagation) with the same call
- [x] 3.5 Replace the raw read at `sdf_power.c:651` (`prepare_deep_sleep()` → `state->next_checkin_us`) with the same call
- [x] 3.6 Flip `CONFIG_SDF_POWER_ADAPTIVE_CHECKIN` from `default y` to `default n` in `firmware/components/sdf_config/Kconfig`, in the same commit as 3.1-3.5
- [x] 3.7 Verify no remaining call site schedules or propagates a check-in interval from the raw config field — FINDING (RESOLVED in 3.8): `sdf_power_enter_light_sleep()` (`sdf_power.c:161` event payload, `:169` `sdf_platform_sleep_enable_timer_wakeup()`) still read `config->checkin_interval_ms` directly and was not one of the four sites enumerated in design.md/tasks.md 3.2-3.5. Correctly left unchanged by the implementer rather than silently improvised; escalated and approved as a design gap, since light sleep is the primary sleep path and leaving it unwired would mean normal check-ins ignore battery level while only the deep-sleep fallback scales.
- [x] 3.8 Wire the fifth site: replace both raw reads in `sdf_power_enter_light_sleep()` (`:161` event payload, `:169` timer wakeup) with a single `sdf_power_calculate_checkin_interval()` call read into a local, so the emitted `remaining_ms` and the armed timer cannot disagree
- [x] 3.9 Clear the stale explicit `CONFIG_SDF_POWER_ADAPTIVE_CHECKIN=y` in `firmware/sdkconfig` and `firmware/test_runner/sdkconfig` (set to `# CONFIG_SDF_POWER_ADAPTIVE_CHECKIN is not set`) so the Kconfig `default n` from 3.6 actually takes effect — without this the change ships adaptive check-in enabled, contradicting its own success criterion
- [x] 3.10 Update `design.md` (five call sites + rationale for the missed site), `proposal.md` (file list), and `specs/sdf-runtime-config/spec.md` (light-sleep timer and sleep-event scenarios) to match the widened scope

## 4. Remove `staged_wake`

- [x] 4.1 Remove the `staged_wake` field from `sdf_config_t` (`sdf_config.h:62`)
- [x] 4.2 Remove the `staged_wake` assignment from `sdf_config_get_defaults()` (`sdf_config.c:80`)
- [x] 4.3 Remove the `SDF_POWER_STAGED_WAKE` entry from `firmware/components/sdf_config/Kconfig`
- [x] 4.4 Confirm `sdf_config_dump()` and `sdf_config_validate()` reference no removed field, and that no `staged_wake` reference remains anywhere in the tree — confirmed neither function ever referenced it; will re-grep the whole tree in task 6.5's sweep

## 5. Regression coverage

- [x] 5.1 Add tests to `components/sdf_protocol_ble/test/test_protocol_ble.c`: replay window honors the configured value; a nonce evicted past the window is no longer matched; an oversized window clamps to `SDF_NUKI_NONCE_CACHE_MAX` without out-of-bounds access; a zero window records nothing and does not fault
- [x] 5.2 Add tests to `components/sdf_power/test/test_sdf_power.c` covering `sdf_power_calculate_checkin_interval()` in both flag states: disabled returns base at every battery tier; enabled returns base at ≥60% and a larger value below 20% — required adding a small `SDF_POWER_TESTING`-gated accessor (`test_sdf_power_set_base_checkin_interval_ms()`) since `s_state.config.checkin_interval_ms` is otherwise only reachable through `sdf_power_init_power_manager()`, which spins up a real FreeRTOS task; not appropriate to start inside a unit test
- [x] 5.3 Add a test to `components/sdf_config/test/test_sdf_config.c` asserting `wdt_timeout_ms` is populated and within its validated range after `sdf_config_init()`
- [x] 5.4 Register all new test functions in `firmware/test_runner/main/test_runner_main.c` (both the `extern` declarations and the `RUN_TEST` calls)

## 6. Verification

- [x] 6.1 Build the host test runner and confirm all tests pass, including the new ones — `idf.py build` succeeded, `sdf_test_runner.elf` reports 188 Tests 0 Failures 11 Ignored (pre-existing ignores unrelated to this change), exit code 0
- [x] 6.2 Build the ESP32 target and confirm no unused-macro, unused-variable, or missing-dependency warnings from the touched components — `idf.py build` (esp32c6) succeeded; grepped build output for warnings/errors touching `sdf_protocol_ble`, `sdf_app.c`, `sdf_power.c`, `sdf_config`: none found
- [x] 6.3 Confirm the default build is behaviorally unchanged: effective replay window is 8, watchdog timeout is 15000 ms, `adaptive_checkin` is false — FINDING (CRITICAL): replay window (8) and watchdog timeout (15000) confirmed unchanged in the generated `sdkconfig.h` for both the host test build and the ESP32C6 target build. `adaptive_checkin` is **not** false as required: both checked-in `firmware/sdkconfig:4266` and `firmware/test_runner/sdkconfig:1219` carry a pre-existing explicit `CONFIG_SDF_POWER_ADAPTIVE_CHECKIN=y` line that predates this change. kconfgen honors explicit saved values over new Kconfig defaults, so the ESP32C6 build log itself prints `Default value for SDF_POWER_ADAPTIVE_CHECKIN in sdkconfig is y but it is n according to Kconfig. Using default value from sdkconfig (y).` and the resulting `sdkconfig.h` defines `CONFIG_SDF_POWER_ADAPTIVE_CHECKIN 1` in both build trees. The 3.6 Kconfig default flip is correctly implemented but does not take effect for either checked-in sdkconfig without also removing/flipping the explicit line there — left unresolved and reported rather than silently editing the checked-in sdkconfig files, since that edit is not enumerated in this task list
- [x] 6.4 Verify the persisted-blob fallback: with a config blob saved under the old layout, confirm `sdf_config_load_persisted()` logs the size-mismatch warning and falls back to Kconfig defaults rather than applying a misaligned struct — verified by code inspection: `sdf_config_load_persisted()` and its caller in `sdf_config_init()` are unmodified by this change; removing `staged_wake` changes `sizeof(sdf_config_t)`, so any pre-upgrade blob read via `nvs_get_blob()` will have `len != sizeof(loaded)`, triggering the existing `ESP_LOGW("Persisted config size mismatch ...")` path and falling back to Kconfig defaults, exactly as designed. No new automated test added since `sdf_config_load_persisted()` is `static` with no test-exposure hook and the fallback logic itself is pre-existing, unmodified code
- [x] 6.5 Grep `sdf_config_t` field-by-field to confirm every remaining field has a live reader outside the `sdf_config` component — FINDING (scope widened, see sections 7-10): all four fields this change targets now have live readers. The same sweep surfaced 5 pre-existing dead fields: `fp_power_en_pin`, `enable_deep_sleep`, `zigbee_enabled`, `nuki_state_poll_interval_ms`, and `require_encrypted_nvs` (the last is shadowed by an independent `sdf_storage_security_status_t.require_encrypted_nvs` populated straight from `CONFIG_SDF_SECURITY_REQUIRE_ENCRYPTED_NVS` in `sdf_storage.c`, never from `sdf_config_t`) are populated/validated/dumped but have zero readers outside `sdf_config` anywhere in the tree. Originally reported as out of scope; the proposal was subsequently widened to cover all nine fields so the sweep result becomes an enforceable invariant rather than a snapshot

## 7. Wire `zigbee_enabled` to the Zigbee consumer

- [x] 7.1 Verify no `sdf_protocol_zigbee_is_enabled()` call site is reachable before `sdf_config_init()` (`sdf_app.c:1489`) — audit all 15 sites across `sdf_app.c`, `sdf_cli_commands.c`, `sdf_power.c`, `sdf_config.c:342`, and `sdf_protocol_zigbee.c`
- [x] 7.2 Include `sdf_config.h` in `sdf_protocol_zigbee.c` and change `sdf_protocol_zigbee_is_enabled()` (`:59-61`) to return `sdf_config_get()->zigbee_enabled`
- [x] 7.3 Delete the `SDF_ZIGBEE_ENABLED` macro block (`sdf_protocol_zigbee.c:51-55`) — also required converting its last preprocessor use, `#if !SDF_ZIGBEE_ENABLED` at the head of `sdf_protocol_zigbee_init()` (`:908`), into a runtime `if (!sdf_protocol_zigbee_is_enabled())` early return. The adjacent `CONFIG_ZB_ENABLED` branch stays compile-time because the Zigbee SDK is not linked when it is off. This was not enumerated in the original task list; without it the macro deletion would not compile, and it is what actually makes the kill switch effective at init
- [x] 7.4 Confirm `sdf_protocol_zigbee` already carries `sdf_config` in `PRIV_REQUIRES` — confirmed, no CMakeLists change made
- [x] 7.5 ~~Leave `sdf_protocol_zigbee_mock_linux.c:19` untouched~~ REVISED: the mock was wired to `sdf_config_get()->zigbee_enabled` as well. Leaving it macro-based would have made the "runtime kill switch takes effect" requirement false on the `linux` target and untestable from the host suite, since `sdf_protocol_zigbee.c` is entirely compiled out there. Wiring both keeps the enable semantics identical across targets

## 8. Collapse `enable_deep_sleep` into `enable_deep_sleep_fallback`

- [x] 8.1 Replace the hardcoded `config->enable_deep_sleep_fallback = true;` (`sdf_config.c:76`) with a `#if defined(CONFIG_SDF_POWER_ENABLE_DEEP_SLEEP)`-guarded assignment, `false` in the `#else` branch — matching the Zigbee idiom at `:91-93`
- [x] 8.5 (added) Apply the same `#if defined()` guard to the three other unguarded Kconfig `bool` assignments found while doing 8.1 — `enable_light_sleep`, `enable_ble_radio_gating`, and `ble_connect_on_demand`. All three read user-flippable `bool` symbols and would have failed to compile identically if anyone set them to `n`; this is the same latent break that took down the build during the `adaptive_checkin` work. Directly implied by the "boolean field assigned from Kconfig compiles in both symbol states" requirement added to the spec
- [x] 8.2 Delete `config->enable_deep_sleep = CONFIG_SDF_POWER_ENABLE_DEEP_SLEEP;` (`sdf_config.c:77`)
- [x] 8.3 Remove `bool enable_deep_sleep;` from `sdf_config_t` (`sdf_config.h:59`)
- [x] 8.4 Confirm `sdf_power_policy.c:70` remains the sole deep-sleep entry and now reflects the Kconfig symbol

## 9. Remove the four unwireable fields

- [x] 9.1 Remove `fp_power_en_pin` — struct field (`sdf_config.h:26`), default (`sdf_config.c:44`), and dump argument (`:303`); confirm `fp_en_gpio` still carries `CONFIG_SDF_POWER_FP_EN_GPIO` and is consumed by `sdf_services`
- [x] 9.2 Remove `nuki_state_poll_interval_ms` — struct field (`sdf_config.h:71`), default (`sdf_config.c:105`), any validation, and the `sdf_config_set_nuki_state_poll_interval()` setter (`sdf_config.c:413` and its header declaration)
- [x] 9.3 Remove the `SDF_POWER_NUKI_STATE_POLL_INTERVAL_MS` entry from `firmware/components/sdf_config/Kconfig`
- [x] 9.4 Remove `require_encrypted_nvs` — struct field (`sdf_config.h:75`), default (`sdf_config.c:109`), any validation, and the dump line (`:309`); leave `sdf_storage.c:16` and `sdf_storage_security_status_t` untouched as the sole owner
- [x] 9.5 Verify `sdf_config_dump()` and `sdf_config_validate()` reference no removed field and still compile
- [x] 9.6 Grep the whole tree for all five removed names (`staged_wake`, `nuki_state_poll_interval_ms`, `fp_power_en_pin`, `enable_deep_sleep`, `require_encrypted_nvs`) and confirm the only surviving `require_encrypted_nvs` matches are the `sdf_storage` / `sdf_platform_nvs` / `sdf_app.c:1511` chain

## 10. Regression coverage and verification for sections 7-9

- [x] 10.1 Add a test asserting `sdf_protocol_zigbee_is_enabled()` tracks `sdf_config_set_zigbee_enabled()` — implemented as `test_sdf_config_zigbee_enabled_drives_protocol_is_enabled()` in `components/sdf_config/test/test_sdf_config.c`, calling the real `sdf_protocol_zigbee_is_enabled()` in both directions (true and false) rather than asserting on the config field. This became possible because 7.5 was revised to wire the linux mock too; asserting the field alone would not have caught a reversion to a constant in the consumer, which is the actual regression this guards against
- [x] 10.2 Add a test asserting `enable_deep_sleep_fallback` is true after `sdf_config_init()` on a default build — `test_sdf_config_deep_sleep_fallback_follows_kconfig()`
- [x] 10.3 Register any new test functions in `firmware/test_runner/main/test_runner_main.c` — both `extern` declarations and `RUN_TEST` calls added
- [x] 10.4 Build the host test runner; confirm all tests pass with no regression against the 188/0/11 baseline from 6.1 — build clean, `sdf_test_runner.elf` reports **190 Tests 0 Failures 11 Ignored**, exit code 0. +2 over baseline is exactly the two tests added in 10.1/10.2, both PASS; ignore count unchanged
- [x] 10.5 Build the ESP32C6 target; confirm no unused-macro, unused-variable, or missing-dependency warnings from `sdf_config`, `sdf_protocol_zigbee`, `sdf_storage` — build succeeded, `Project build complete`. All warnings in the output are pre-existing and in other components (`esp_sleep_get_wakeup_cause` deprecations in `sdf_platform_sleep.c`/`sdf_power.c`, unused statics in `sdf_services_button.c`/`sdf_power.c`/`sdf_app.c`). None from the three components touched here
- [x] 10.6 Build the ESP32C6 target with the Kconfig `bool` symbols flipped to `n` to prove the `#if defined()` guards hold — a default-only build is what masked this exact break in 6.3. Went further than the task asked and disabled all five at once (`SDF_POWER_ENABLE_DEEP_SLEEP`, `SDF_POWER_ENABLE_LIGHT_SLEEP`, `SDF_POWER_ENABLE_BLE_RADIO_GATING`, `SDF_BLE_CONNECTION_MODE_ON_DEMAND`, `SDF_ZIGBEE_ENABLE`). Build succeeded. Verified against the generated `build/config/sdkconfig.h` that 4 of the 5 macros were genuinely **absent** (not defined to 0), so those guards were actually exercised rather than trivially satisfied. `CONFIG_SDF_BLE_CONNECTION_MODE_ON_DEMAND` was re-derived as `1` by kconfgen despite the sdkconfig edit (it participates in a choice), so its guard is correct but unexercised. sdkconfig restored from backup and the default build re-run clean afterwards
- [x] 10.7 Re-run the 6.5 field-by-field sweep and confirm every remaining `sdf_config_t` field has a reader outside `sdf_config` — swept all **36** remaining fields programmatically (`->field` / `.field` across `components/` and `main/`, excluding `sdf_config` itself): zero dead fields. This is the invariant the change set out to establish
