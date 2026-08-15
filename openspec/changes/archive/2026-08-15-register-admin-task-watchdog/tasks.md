## 1. Pre-flight: confirm the bootstrap callback is safe

- [x] 1.1 Trace the `admin_action_cb` registered into `sdf_services_config_t` by `sdf_app`
      and establish its worst-case duration for the non-ENROLL local-physical actions
      (Nuki pairing, factory reset) reachable through
      `sdf_services_try_bootstrap_admin_action()` on an unclaimed device.
      **Done.** `sdf_app_on_admin_action()` (`sdf_app.c:433`); worst branch is
      FACTORY_RESET (`sdf_app.c:522`), five steps. Only step 2 (`fp_delete_all_users()`)
      approaches the budget at 12 s (`CONFIG_SDF_FP_RESPONSE_TIMEOUT_MS=12000`); steps
      1/3/4 are sub-second. See the table in design.md Risks.
- [x] 1.2 If that callback can block longer than the 15s TWDT window configured at
      `sdf_app.c:1638`, STOP and raise a separate change to make it dispatch rather than
      block.
      **Gate does not trigger — proceed.** `fp_wait_for_reply()`
      (`fingerprint.c:1138-1145`) already blocks in 1 s slices and calls
      `esp_task_wdt_reset()` per slice, so step 2 self-feeds. That mitigation is currently
      a silent no-op for the admin task, because `esp_task_wdt_reset()` returns
      `ESP_ERR_NOT_FOUND` for an unsubscribed task and the result is discarded —
      registering the admin task is what activates it.

## 1b. Follow-on findings (not this change)

- [ ] 1b.1 Raise separately: `sdf_ble_companion.c:1182` calls
      `ble_store_util_bonded_peers()` on an explicitly documented but unguarded assumption
      that `ble_hs` is already initialized. When it is not, the result is a NULL-mutex load
      fault, not an error return. Surfaced by the emulator (see design.md); benign on real
      hardware, but the site has no failure path.
- [x] 1b.2 Consider: `fp_wait_for_reply()`'s discarded `esp_task_wdt_reset()` return value
      is what let this defect hide. Logging `ESP_ERR_NOT_FOUND` once would have named the
      unregistered caller directly.
      **Carried forward** into `route-service-tasks-through-platform-wdt` task 1.1, which
      puts the one-shot diagnostic in `sdf_platform_time_wdt_reset()` so every adopter gets
      it, rather than patching the one call site where it was noticed.

## 2. Platform watchdog wrappers

- [x] 2.1 Declare `sdf_platform_time_wdt_add(void)` and
      `sdf_platform_time_wdt_delete(void)` in
      `firmware/components/sdf_platform/include/sdf_platform_time.h`, documented in the
      same style as the existing `sdf_platform_time_wdt_reset()` at line 73.
- [x] 2.2 Implement both in `sdf_platform_time.c` with the
      `#ifndef CONFIG_IDF_TARGET_LINUX` guard held *inside* the function, matching the
      existing `wdt_reset` / `wdt_feed` shape — callers must never see the guard.
- [x] 2.3 Give the Linux path observable bookkeeping: record registered task handles in a
      small fixed-size table and expose a query for tests. It must tolerate a handle
      vanishing without a matching `wdt_delete`, because `sdf_services_stop_tasks()` can
      force-delete a task.
- [x] 2.4 **Expect a host link failure the moment 2.3 lands, and fix it here.**
      `sdf_platform_time.c` carries an undefined reference to `esp_timer_get_time_mock()`
      on the Linux target — declared in `sdf_mock_linux_time.h:11`, macro-substituted at
      line 15, implemented nowhere. It has never surfaced because nothing references the
      object's symbols, so the linker drops it entirely (`nm sdf_test_runner.elf` shows no
      `sdf_platform_time_wdt_*` and no `esp_timer_get_time_mock`). This change is the first
      host-target caller, which pulls the object into the link. Implement the mock; it is a
      one-liner over `clock_gettime`, and `sdf_platform_time.c:13` is its only consumer.
      Without this the break presents as an unrelated-looking test-runner link error.

## 3. Register the admin task

- [x] 3.1 Call `sdf_platform_time_wdt_add()` on entry to `sdf_admin_task`
      (`sdf_services_admin.c:194`), before the main loop.
- [x] 3.2 Call `sdf_platform_time_wdt_reset()` at the *top* of the main loop, ahead of the
      `stop_requested` check and the `wait_ms` computation — not at the bottom, so the
      `continue` in the action-timeout branch (line 295) cannot skip it.
- [x] 3.3 Call `sdf_platform_time_wdt_delete()` on the cooperative shutdown path, after
      `sdf_admin_task_deinit_queue()` and before `vTaskDelete(NULL)`, mirroring the
      ordering in `sdf_match_task` / `sdf_enroll_task`.
- [x] 3.4 Delete the now-unused `#include "esp_task_wdt.h"` and its surrounding `#ifndef`
      at `sdf_services_admin.c:12-14`. The file should end up with no direct reference to
      `esp_task_wdt` at all.

## 4. Tests

- [x] 4.1 Extend `test_sdf_services_start_stop_start_tasks_cycle`
      (`test_sdf_services.c:897`) to assert the admin task is watchdog-registered after
      `sdf_services_start_tasks()` and no longer registered after
      `sdf_services_stop_tasks()`, across the existing start → stop → start cycle so
      double-registration on restart is caught.
- [x] 4.2 Run the host suite via the `test_runner` linux build and confirm the new
      assertions fail when 3.1 is reverted — the point of this change is that the omission
      becomes detectable, so verify the detector actually detects.

## 5. Verification

- [x] 5.1 Build for the ESP target and confirm no `esp_task_wdt` link or include
      regressions in `sdf_services` or `sdf_platform`.
- [ ] 5.2 ~~Verify on `esp-emu`~~ — **not achievable, do not attempt.** The current merged
      image boot-loops under `esp-emu` v0.39.0 (esp32c6) with a NULL-`ble_hs_mutex` load
      fault ~615 ms in, before the services tasks reach steady state; the emulator brings
      up no BT controller without an `--ble-hci` backend. Even if bypassed, it models
      neither the fingerprint UART peer, the Zigbee radio, nor the BT controller — the
      three peripherals whose timing this change depends on — so its numbers would be
      meaningless. Full evidence in design.md Risks.
- [ ] 5.3 On real hardware: confirm an idle device (no events, no pending action) runs well
      past 15 s without tripping the TWDT, exercising the `SDF_ADMIN_IDLE_WAIT_CAP_MS`
      bounded-wait path.
- [ ] 5.4 On real hardware, with the fingerprint sensor disconnected: long-press the button
      on an unclaimed device to drive the bootstrap FACTORY_RESET path, and confirm the
      12 s `fp_delete_all_users()` timeout elapses without a TWDT panic. This is the
      scenario the whole change turns on and the one the host suite cannot reach.
