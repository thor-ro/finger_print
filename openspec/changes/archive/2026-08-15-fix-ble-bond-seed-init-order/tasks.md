# Tasks

## 1. Confirm the defect on the current build

- [x] 1.1 Confirm `nimble_port_init()` appears exactly once in the tree and is
  reached only via `sdf_nuki_ble_init()` (`sdf_nuki_ble_transport.c:703`).
- [x] 1.2 Confirm `sdf_ble_companion_init()` (`sdf_app.c:1810`) precedes
  `sdf_nuki_ble_init()` (`sdf_app.c:1865`) straight-line in `sdf_app_init()`.
- [x] 1.3 Confirm the failure mechanism: `ble_npl_mutex_pend()`
  (`nimble_npl_os.h:224`) dereferences `mu->handle` without validating it, so
  the `rc != 0` branch at the call site cannot be reached.
- [x] 1.4 Reproduce the panic under `esp-emu` and symbolize the backtrace to
  `sdf_ble_companion.c:1182`.
- [x] 1.5 Confirmed on real hardware. Flashed to esp32c6 over USB: continuous
  boot loop, 15 reboots in 25 s, load access fault `MTVAL=0x44` at ~1017 ms,
  symbolized end to end from `sdf_app.c:1810` to `ble_npl_mutex_pend`. The
  device does not boot. This raises the change from "robustness" to
  "ship-blocking".

## 2. Audit before moving

- [x] 2.1 `ble_store_util_bonded_peers()` at line 1182 was the *only* NimBLE
  host call in `sdf_ble_companion_init()`. `sdf_nuki_ble_register_server_service()`
  only appends to `s_server_services[]`; `sdf_ble_companion_register_gatt()`
  (which does call `ble_gatts_count_cfg`) runs later as an `init_cb` invoked
  from inside `nimble_port_init()`'s caller, which is correct.
- [x] 2.2 `s_lock` is required — `s_bond_state` is documented as
  "Protected by s_lock", and on the host task the seed now races GAP events and
  `sdf_ble_companion_open_pairing_window()` (services task). No deadlock: this
  file's invariant is that `s_lock` is never held across a NimBLE call
  (cf. `sdf_ble_companion_push_allow_list()`), and the new code keeps it —
  `ble_store_util_bonded_peers()` runs lock-free, `s_lock` only wraps the
  `bond_allow_list_add()` loop.
- [x] 2.3 Confirmed. `ble_gap_wl_set()` appears once, in
  `sdf_ble_companion_push_allow_list()`, reached from
  `sdf_ble_companion_restart_advertising()` and the `enc_change` handler.
  Seeding ahead of `restart_advertising()` is sufficient.

## 3. Move the seeding

- [x] 3.1 Removed from `sdf_ble_companion_init()`, together with the false
  "assumes the NimBLE host/bond store is already initialized" comment. Replaced
  with a note stating the constraint that made it a bug.
- [x] 3.2 Added `sdf_ble_companion_seed_allow_list()`, called from the top of
  `sdf_ble_companion_on_host_sync()` before `restart_advertising()`.
- [x] 3.3 Guarded by `s_allow_list_seeded`, cleared in `init()` next to the
  `sdf_ble_companion_bond_state_init()` that resets what it seeds — so the
  guard tracks the bond state's lifetime, not literally the boot, and a
  deinit/init cycle re-seeds correctly while a NimBLE resync does not.
- [x] 3.4 `rc != 0` kept as warning-and-continue, with a comment noting the
  branch is now genuinely reachable.
- [x] 3.5 Locking applied per 2.2.

## 4. Tests — NOT DONE, see 4.4

- [ ] 4.1 Regression assertion that `sdf_ble_companion_init()` performs no
  NimBLE host calls.
- [ ] 4.2 Cover the seed guard: a second `on_host_sync()` must not re-add peers.
- [ ] 4.3 Cover the failure path: seeding returning non-zero leaves the device
  initialized with an empty allow list.
- [x] 4.4 **Decision: 4.1-4.3 are not covered by automated tests.**
  `sdf_ble_companion` is absent from the linux test runner's component list
  (`firmware/test_runner/CMakeLists.txt`) because it depends on `bt`/NimBLE,
  which has no linux-target build. A grep-based CI guard was considered for
  4.1 and rejected as unprecedented — `firmware-ci.yml` has no such checks, and
  adding the first one is a separate decision, not part of this fix. The fix
  is verified by hardware boot (5.4) instead. **This is a real coverage gap:
  a future edit could reintroduce a pre-host-init NimBLE call and nothing
  would catch it before flashing.** Tracked as 6.3.

## 5. Verification

- [x] 5.1 `idf.py build` clean for `esp32c6`. `sdf.bin` 0x10fdf0, 45% free.
- [x] 5.2 Host suite green: 285 Tests, 0 Failures, 11 Ignored — unchanged from
  the pre-change baseline.
- [ ] 5.3 `esp-emu` boot check — superseded by the hardware run in 5.4, which
  is strictly better evidence. Still worth doing to restore the emulator as a
  cheap smoke gate.
- [x] 5.4 **Verified on hardware.** Flashed to the connected esp32c6: no
  `Guru Meditation` panic, `sdf_app_init()` completes. Boot log shows
  `GAP procedure initiated: set whitelist; count=0` (seed ran, no bonds on this
  board) followed by `Sparse, allow-list-filtered advertising started`, then
  full app startup through `sdf_power: Power manager started`. The BLE boot
  loop is gone.
- [ ] 5.5 Not yet verified: a companion bonded *before* a reboot reconnecting
  afterwards without re-pairing. This board has no bonds, so the seed ran with
  `count=0` and the non-empty path is still unexercised on hardware.

## 6. Follow-on, out of scope here

- [ ] 6.1 `esp-emu --ble-hci` for a controller-backed boot smoke test. Useful
  regardless, but the boot path must not depend on it.
- [ ] 6.2 Audit the other `*_init()` functions called from `sdf_app_init()` for
  the same class of defect — subsystem init reading state owned by a subsystem
  initialized later in the same function.
- [ ] 6.3 Close the coverage gap from 4.4: either bring `sdf_ble_companion`
  under host test with a NimBLE seam, or establish a static-check convention
  in `firmware-ci.yml`.
- [x] 6.4 **Separate defect, uncovered once the device booted far enough to
  reach it. Now addressed by `fix-zigbee-commissioning-and-idle-wdt`.**
  At ~18 s the idle task watchdog aborts:
  `task_wdt: - IDLE (CPU 0)` / `Tasks currently running: CPU 0: sdf_zigbee`,
  symbolizing to `zb_mac_logic_iteration` in the closed-source
  `esp-zigbee-lib`. `sdf_protocol_zigbee` retries network steering
  (`status=ESP_FAIL`) on a unicore chip and starves IDLE; `sdf_app.c:1638`
  configures the TWDT with `idle_core_mask` set and `trigger_panic = true`, so
  it panics and reboots. Pre-existing and unrelated to this change —
  `sdf_app.c` and `sdf_protocol_zigbee` are untouched here; it was simply
  masked by the earlier BLE panic at ~1 s. Needs its own change.
