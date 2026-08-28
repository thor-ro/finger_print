# Workstream A — Security & trust boundaries

## A-1 — Biometric lockout and failed-attempt state do not survive a reboot or deep sleep

**Severity:** S2 (Major) **Confidence:** high
**Files:** `firmware/components/sdf_services/src/sdf_services_match.c:126-232`, `firmware/components/sdf_power/src/sdf_power.c:335-352`, `firmware/components/sdf_platform/include/sdf_platform_sleep.h:29-39`
**Change proposal:** `openspec/changes/persist-biometric-lockout/`

### Evidence

The brute-force control lives entirely in RAM. `sdf_match_task_run_match_cycle()` keeps the counter and the deadline in the services state struct:

```c
s->failed_attempt_count++;
if (s->failed_attempt_count >= failed_attempt_threshold) {
    s->lockout_until_us = now_us + ((int64_t)lockout_duration_ms * 1000LL);
```

Neither `failed_attempt_count` nor `lockout_until_us` is written to NVS — `grep -n "lockout\|failed_attempt" sdf_storage/` returns nothing. Both are plain members of the RAM-resident services state, so a reset, a battery pull, or a deep-sleep cycle zeroes them and `run_match` is unblocked immediately on the next boot.

The deep-sleep case is worse than an oversight — the mechanism to carry this across sleep exists and is stubbed out. `sdf_power_retention_t` reserves a field for it (`sdf_platform_sleep.h:35`):

```c
uint8_t  enrolled_user_count;
uint32_t failed_attempts;
```

but the only production deep-sleep path zero-initialises the struct and fills in just two timestamps before writing it (`sdf_power.c:344-350`):

```c
sdf_power_retention_t retention = {0};
retention.last_activity_us = esp_timer_get_time();
retention.next_checkin_us  = retention.last_activity_us + ...;
sdf_power_save_retention(&retention);
```

So `failed_attempts` is written as 0 on every deep sleep. And nothing ever reads it back: `sdf_power_load_retention()`, `sdf_power_get_retention_state()`, `sdf_power_prepare_deep_sleep()` and `sdf_power_resume_from_deep_sleep()` have **no callers anywhere in the tree**, tests included. The retention block is write-only.

### Impact

`AGENTS.md` and `README.md` both advertise biometric brute-force protection as a security default (5 failures in 60 s → 120 s lockout). In practice:

- **An attacker who can interrupt power clears an active lockout instantly.** This is a door-mounted, battery-powered reader; power is exactly the resource an attacker at the door has. The 120 s penalty becomes a reboot.
- **A normal deep-sleep cycle silently drops an active lockout**, with no attacker involved. Deep-sleep fallback is enabled by default and triggers when Zigbee has not joined — i.e. permanently, on a BLE-only installation. There, the lockout expires at the next sleep rather than at its deadline.

The residual risk is bounded by the sensor's false-accept rate rather than by the lockout, which is the opposite of the documented posture. Rated S2 rather than S1 because defeating it still requires beating the sensor's FAR over many attempts; it is a defeated control, not a direct bypass.

### Fix

See `openspec/changes/persist-biometric-lockout/`. Persist lockout entry/clear to NVS (two writes per lockout episode, not per attempt), re-arm a full lockout period at boot when the persisted flag is set — elapsed wall-clock time is unknowable across power loss, so a power cycle must cost the attacker a fresh lockout rather than clear one — and delete the dead `failed_attempts` retention field so one mechanism owns this.

### Verification

Host Unity tests in `sdf_services` for arm/persist/restore/clear, plus an on-chip esp-emu run that enters lockout, resets, and confirms matching is still refused after boot.

### Related observation (not filed separately)

`enrolled_user_count` and `sensor_power_state` in `sdf_power_retention_t` are dead in the same way — always written as 0, never read. The whole retention read path is unreachable code. Carried into workstream C (power) rather than fixed here.
