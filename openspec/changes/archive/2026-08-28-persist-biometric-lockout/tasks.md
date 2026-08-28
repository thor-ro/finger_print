## 1. Storage record

- [x] 1.1 Add `sdf_storage_lockout_save(bool armed)` / `sdf_storage_lockout_load(bool *armed)` / `sdf_storage_lockout_clear(void)` to `sdf_storage`, following the `sdf_storage_<domain>_*` naming and error conventions of the existing records. Evidence: `sdf_storage.h:126-128`, `sdf_storage.c:719-790` (`"bio_lockout"` u8 in the existing `"nuki"` namespace).
- [x] 1.2 Ensure the new key is erased by `sdf_storage_erase_all()` (factory reset must clear a lockout). Evidence: `sdf_storage_erase_all()` calls `nvs_flash_erase()`, which wipes the whole partition including this key; asserted by `test_sdf_storage_lockout_cleared_by_erase_all`.
- [x] 1.3 `sdf_storage_lockout_load()` returns `ESP_ERR_NOT_FOUND` on a fresh device; callers treat that as "not locked out". Evidence: `sdf_storage.c:735-772` plus `test_sdf_storage_lockout_load_absent_on_fresh_device`. **Deviation from the original one-liner, found while writing 5.6**: the loader used to collapse *every* `nvs_open` failure into `ESP_ERR_NOT_FOUND`. Since the caller does not log the NOT_FOUND case (it is the normal fresh-device path), that made a genuine storage glitch indistinguishable from a device that has never locked out and left 3.3's "log the failure" unsatisfiable. Only `ESP_ERR_NVS_NOT_FOUND` is now mapped to `ESP_ERR_NOT_FOUND`; anything else is handed back verbatim.

## 2. Match-task persistence

- [x] 2.1 At the `emit_lockout` site (`sdf_services_match.c:219-225`), persist "armed" after `xSemaphoreGive(s->lock)` — never under the lock. Evidence: `sdf_services_match.c:324` (`sdf_storage_lockout_save(true)`), in the post-scan block that runs after the lock is released.
- [x] 2.2 At the `lockout_cleared` site (`:130-135`) and on the successful-match reset (`:228-230`), persist "cleared", also outside the lock. Evidence: `sdf_services_match.c:192` (expiry) and `:294` (successful match), both `sdf_storage_lockout_clear()` outside the lock. Both are guarded by `lockout_persist_armed` so an episode writes NVS exactly twice, never once per failed attempt (D3).
- [x] 2.3 Log but do not fail the match cycle if the NVS write fails; a lockout that cannot be persisted still applies for the current boot. Evidence: all three sites `ESP_LOGW` and continue; the RAM deadline is set/cleared independently of the write result.

## 3. Boot restore

- [x] 3.1 In `sdf_services_init()`, load the persisted flag alongside the enrolled-user cache load, before the match task is created. Evidence: `sdf_services.c:1035`.
- [x] 3.2 When armed, set `s->lockout_until_us = esp_timer_get_time() + lockout_duration_ms` from the config in force at boot; leave `failed_attempt_count` at 0. Evidence: `sdf_services.c:1035-1044`; asserted by `test_lockout_restore_arms_full_duration_from_boot` and, on a real chip target across a real reset, by the `lockout_reset_gate` (7.3).
- [x] 3.3 Treat a missing or corrupt record as "not locked out", logging the corrupt case (D4). Evidence: `sdf_services.c:1046` (`W sdf_services: Failed to load lockout state, treating as not locked out: ...`, observed live in the host run); `test_lockout_restore_absent_record_permits_matching`, `test_lockout_restore_wrong_typed_record_reads_as_absent`, `test_lockout_restore_unreadable_record_permits_matching`.
- [x] 3.4 Ensure the restored lockout emits `SECURITY_LOCKOUT` (CRITICAL) and its eventual expiry emits the NORMAL clear, preserving the `security-event-unification` pairing (D5). Evidence: `sdf_services_match.c:160-184` announces from the first match cycle rather than from `init()` (the router is not guaranteed running until after init returns) and only consumes the pending flag once the emission actually happened; `test_lockout_restore_announces_critical_event` and the gate's `announced=CRITICAL`.

## 4. Retire the dead retention field

- [x] 4.1 Remove `uint32_t failed_attempts` from `sdf_power_retention_t` (`sdf_platform_sleep.h:29-39`).
- [x] 4.2 Remove the `state->failed_attempts = 0` assignment in `sdf_power_prepare_deep_sleep()` (`sdf_power.c:679-697`).
- [x] 4.3 Confirm no reader breaks — `sdf_power_load_retention()` still has no callers after the change. Evidence: repo-wide grep finds only its definition (`sdf_power.c:657`) and declaration (`sdf_power.h:79`); the remaining `failed_attempts` hits are the unrelated `sdf_event_router` security-event payload field.

## 5. Tests

- [x] 5.1 `sdf_storage`: save/load/clear round-trip, and `erase_all()` clears the record. Evidence: `test_sdf_storage_lockout_save_load_roundtrip`, `..._clear_makes_record_absent`, `..._load_absent_on_fresh_device`, `..._load_rejects_null`, `..._cleared_by_erase_all`.
- [x] 5.2 `sdf_services`: entering lockout persists armed. Evidence: `test_lockout_entry_persists_armed_record`, plus `test_lockout_failed_attempt_below_threshold_writes_nothing` for the flash-wear bound (D3).
- [x] 5.3 `sdf_services`: a successful match clears the persisted flag. Evidence: `test_lockout_successful_match_clears_stale_persisted_record`.
- [x] 5.4 `sdf_services`: lockout expiry clears the persisted flag. Evidence: `test_lockout_expiry_clears_persisted_record`.
- [x] 5.5 `sdf_services`: init with the flag armed refuses matching and arms a full `lockout_duration_ms` from boot. Evidence: `test_lockout_restore_arms_full_duration_from_boot`, `test_lockout_restore_refuses_matching`, `test_lockout_restore_announces_critical_event`.
- [x] 5.6 `sdf_services`: init with no record, and with a corrupt record, both permit matching. Evidence: `test_lockout_restore_absent_record_permits_matching`, `test_lockout_restore_wrong_typed_record_reads_as_absent` (NVS keys are looked up by name *and* type, so a wrong-typed record reads as `NOT_FOUND`, not `TYPE_MISMATCH`), `test_lockout_restore_unreadable_record_permits_matching` (NVS deinitialised — a genuine load failure, which is what exposed 1.3's deviation).
- [x] 5.7 Register every new test in `firmware/test_runner/main/test_runner_main.c`. Evidence: 15 `extern` declarations + 15 `RUN_TEST` calls; the 9 `sdf_services` ones sit inside the existing `#ifdef CONFIG_IDF_TARGET_LINUX` block (they drive the mock UART seam).

## 6. Documentation

- [x] 6.1 `AGENTS.md` "Security Defaults": state that the lockout is persisted and that a reboot re-arms it. Evidence: the bullet now records the full-duration-from-boot re-arm and *why* a remainder is not recoverable (no battery-backed RTC, `esp_timer_get_time()` restarts at zero), that only a successful match or factory reset clears it, and the two-writes-per-episode flash-wear bound. A "Lockout Reset Gate" section documents the 7.3 fixture and its runner.
- [x] 6.2 `version.md`: entry under the next version. Evidence: `## Unreleased` gains a `### Security` section for the persisted lockout and the `sdf_power_retention_t.failed_attempts` removal, plus an `### Added` line for the 15 new tests.

## 7. Verification

- [x] 7.1 `idf.py build` (esp32c6) clean, no new warnings. Evidence: from-scratch production build exits 0. Eight compiler warnings remain, all pre-existing and untouched by this change (`esp_sleep_get_wakeup_cause` deprecations in `sdf_platform_sleep.c`/`sdf_power.c`, unused `TAG`/statics in `sdf_platform_power.c`/`sdf_app.c`, two in IDF's own `bt.c`), as does the pre-existing `sdf_services`→`sdf_app` include-dir CMake warning.
- [x] 7.2 Host test runner passes on the `linux` target, including the new suites. Evidence: **426 Tests, 0 Failures, 12 Ignored** (411/0/12 before), all 15 new tests `PASS` by name. The run also shows the new fail-open path live: `W sdf_services: Failed to load lockout state, treating as not locked out: ESP_ERR_NVS_NOT_INITIALIZED`.
- [x] 7.3 esp-emu on-chip run: enter lockout, reset the emulated device, confirm matching is still refused after boot and the CRITICAL lockout event is emitted. Evidence: new fixture `firmware/lockout_reset_gate/` + `scripts/run_lockout_reset_gate.sh`, end-to-end `exit=0` with `LOCKOUT_RESET_GATE_RESULT status=PASS detail=restored=1 refused=1 announced=CRITICAL` (`lockout_until_us=120054260` against a `before_init_us=53762`, i.e. a full 120 000 ms from *this* boot, not a remainder). **Three fixture-only deviations, all documented in the fixture and in AGENTS.md**: (a) boot 1 arms via `sdf_storage_lockout_save(true)` rather than five failed scans, because an emulated `fp_match_1n()` can only time out and a timeout is deliberately not a failed attempt; (b) boot 2 seeds one enrolled user so the match cycle gets past its empty-set early return; (c) the image is merged at 4MB — esp-emu v0.40.1 re-detects the flash as 4MB after a soft reset, so an 8MB image header aborts boot 2 in `__esp_system_init_fn_init_flash` before `app_main()`; the partition table ends at `0x400000`, so the layout is unchanged. The one production-side change the fixture needed is a `SDF_EVENT_ROUTER_SUBS_GATE_FIXTURE` bucket in `sdf_event_router_capacity.h`, `0` unless a build defines it, so the gate's own observer is a *declared* subscriber and the zero-headroom guard against *undeclared* ones stays exactly as it was.
- [x] 7.4 `openspec validate persist-biometric-lockout --strict` passes.
