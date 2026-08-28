## Why

The biometric brute-force control is RAM-only. `sdf_match_task_run_match_cycle()` holds both the failure counter and the lockout deadline in the services state struct (`sdf_services_match.c:130-232`); neither is persisted (`sdf_storage` has no lockout key). Any reset, battery pull or deep-sleep cycle zeroes them, and the next boot resumes matching immediately.

This is a door-mounted, battery-powered reader. Interrupting power is precisely the capability an attacker standing at the door has, so the advertised "5 failures in 60 s → 120 s lockout" (`AGENTS.md`, `README.md`) currently costs an attacker one reboot. It also degrades with no attacker at all: deep-sleep fallback is enabled by default and fires whenever Zigbee has not joined — permanent on a BLE-only installation — so an active lockout is dropped at the next sleep rather than at its deadline.

The mechanism intended to carry this across sleep exists and is stubbed out. `sdf_power_retention_t` reserves `uint32_t failed_attempts` (`sdf_platform_sleep.h:35`), but the only production deep-sleep path zero-initialises the struct and fills in two timestamps (`sdf_power.c:344-350`), so the field is written as 0 every time. Nothing reads it back: `sdf_power_load_retention()`, `sdf_power_get_retention_state()`, `sdf_power_prepare_deep_sleep()` and `sdf_power_resume_from_deep_sleep()` have no callers anywhere in the tree, tests included. The retention block is write-only, and a field named `failed_attempts` that is always zero is worse than no field at all.

## Changes

- The match task SHALL persist biometric lockout state to NVS when a lockout is entered and when it is cleared, so the lockout survives a reset, a power loss and a deep-sleep cycle.
- On boot, a device whose persisted state records an armed lockout SHALL re-arm a full `lockout_duration_ms` measured from boot, and SHALL refuse matching for that period. Elapsed wall-clock time is unknowable across power loss (no battery-backed RTC, `esp_timer_get_time()` restarts at 0), so a power cycle costs a fresh lockout instead of clearing one.
- Persistence SHALL occur on lockout entry and clear only — not on every failed attempt — bounding flash writes to two per lockout episode. The accepted consequence is stated under Impact.
- `sdf_storage` SHALL gain a lockout record (`save`/`load`/`clear`) following the existing `sdf_storage_<domain>_*` convention, cleared by `sdf_storage_erase_all()` like every other record.
- The dead `failed_attempts` field SHALL be removed from `sdf_power_retention_t` so NVS is the single mechanism for this state and no stubbed field suggests otherwise.
- Spec delta on `sdf-services-tasks`: lockout state is durable, and a reboot during lockout re-arms rather than clears.

## Impact

**Firmware**

- `sdf_services` (`sdf_services_match.c`): two new persistence call sites at the existing `emit_lockout` / `lockout_cleared` decision points, both outside `s->lock` — an NVS write must not happen under the services lock, matching the existing rule that UART round-trips do not either. `sdf_services_init()` gains a restore step.
- `sdf_storage`: one new record, one new key.
- `sdf_power` / `sdf_platform`: struct field removal only. Safe because nothing reads the retention block; its CRC covers the struct and is recomputed on write.

**Tests**

Host Unity tests in `firmware/components/sdf_services/test/` and `firmware/components/sdf_storage/test/`, registered in `firmware/test_runner/main/test_runner_main.c`. An on-chip esp-emu run covers the boot restore path.

**No migration required** — no devices in the field; a missing NVS record reads as "no lockout", which is the correct default for a device that has never locked out.

**Accepted limitation.** Persisting only at lockout entry means a sub-threshold failure count (up to `threshold - 1` attempts) is still lost on a power cycle, so an attacker who reboots every four attempts keeps a supply of attempts at the cost of one boot each. Persisting every attempt would close this, but hands the same attacker a flash-wear DoS: a paced attack would issue one NVS write per scan cycle indefinitely. Bounding writes to two per lockout episode is the proportionate trade, and it restores the property the documentation actually claims — that an entered lockout is served. If the threat model tightens, per-attempt persistence under an explicit wear budget is the follow-up.

**Not addressed here.** `enrolled_user_count` and `sensor_power_state` in `sdf_power_retention_t` are dead in the same way, and the entire retention read path is unreachable. That belongs to a power-workstream change, not this one.
