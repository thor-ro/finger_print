## 1. Introduce the shared mapping

- [x] 1.1 Add a helper (e.g. `sdf_services_pulse_pending_action_led(sdf_services_admin_action_t)`) that switches over the full `sdf_services_admin_action_t` enum and issues the corresponding `led_pulse_*` call.
- [x] 1.2 Populate it with the union of today's three tables: `ENROLL`/`ENROLL_ADMIN` → blue, `NUKI_PAIR` → yellow, `ZB_JOIN` → purple, `FACTORY_RESET` → red, `WEB_REG_AUTH` → white, `NUKI_REPAIR` → cyan, `BLE_PAIRING_WINDOW` → cyan.
- [x] 1.3 Add an explicit no-op case for `SDF_SERVICES_ADMIN_ACTION_NONE` and any other enumerator with no indication, so no real enumerator relies on `default`.
- [x] 1.4 Declare it in `sdf_services_internal.h`.

## 2. Replace the three inline switches

- [x] 2.1 `sdf_services_button.c`: replace the switch in `sdf_button_dispatch_action()` (currently lines ~171-193) with a call to the helper.
- [x] 2.2 `sdf_services_admin.c`: replace the switch in `sdf_admin_task`'s `SDF_EVENT_ROUTER_ADMIN_ACTION_REQUEST` handler (currently lines ~128-150) with a call to the helper.
- [x] 2.3 `sdf_services.c`: replace the switch in `sdf_services_request_admin_action()` (currently lines ~1105-1114) with a call to the helper.
- [x] 2.4 Delete the now-obsolete comment in `sdf_services.c` (~1099-1104) explaining why the switch was mirrored rather than shared.
- [x] 2.5 Confirm the two call sites that invoke the helper while holding `s->lock` remain correct — the helper must not take any lock and must only issue non-blocking `led_post_cmd()`-backed calls.

## 3. Verification

- [x] 3.1 Build for the target and confirm no unhandled-enumerator warnings.
- [x] 3.2 Run the host test suite (`sdf_services`) and confirm no regressions; update any assertion that depended on a path producing no LED pulse for an action that now pulses.
- [x] 3.3 Add a test asserting that `BLE_PAIRING_WINDOW` produces its cyan indication when set pending via the event-router admin-action path, not only via the button path — this is the specific hole that motivated the change.
- [x] 3.4 Spot-check on hardware or emulator: double-click still pulses cyan (button path unchanged), and a BLE-originated `NUKI_REPAIR` still pulses cyan.
