# sdf_power public setters are silent no-ops in host unit tests

`sdf_power_set_checkin_interval_ms()` and `sdf_power_set_battery_percent()` in
`firmware/components/sdf_power/src/sdf_power.c` only write their target
`s_state` field inside `if (s_state.lock != NULL && xSemaphoreTake(...) == pdTRUE) { ... }`.

`s_state.lock` is created only by `sdf_power_init_power_manager()`, which
starts a real FreeRTOS task and is never called from
`firmware/test_runner/main/test_runner_main.c`'s `app_main()`. Any unit test
that calls these public setters directly in the host (`IDF_TARGET=linux`)
test runner will see the call succeed with no error, but the underlying
`s_state` field silently stays at its zero-initialized value.

Symptom if hit again: a test that sets battery percent to a high value then
asserts `sdf_power_calculate_checkin_interval()` returns the unscaled base
will instead observe the low-battery-scaled result (battery reads back as 0).

Fix/workaround used in `wire-dead-config-fields`: added small
`SDF_POWER_TESTING`-gated test-only accessors directly in `sdf_power.c` that
write `s_state` fields without the lock guard, e.g.
`test_sdf_power_set_base_checkin_interval_ms()` and
`test_sdf_power_set_battery_percent_raw()`. Declare as `extern` in the test
file and call those instead of the public setters when a unit test needs to
seed `s_state` without running the full power manager init. This follows the
existing `SDF_POWER_STATIC`/`SDF_POWER_TESTING` exposure pattern already used
elsewhere in the component.
