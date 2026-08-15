# Tasks

## 1. Fixes

- [x] 1.1 Restore the `else` branch in the `DEVICE_FIRST_START`/`DEVICE_REBOOT`
  handler (`sdf_protocol_zigbee.c:125`), moving
  `sdf_zigbee_set_network_joined(true)` and
  `esp_zb_ota_upgrade_client_query_interval_set()` onto the non-factory-new
  path where they belong, with a comment explaining why that branch is the only
  place the rejoined state can be recorded.
- [x] 1.2 Fix `"Device started in %s factory-reset mode"` → `"in%s"` with
  `? "" : " non"`, removing the double space in the factory-new case.
- [x] 1.3 Set `idle_core_mask = 0` in the TWDT config (`sdf_app.c:1638`),
  keeping `trigger_panic = true`, with a comment recording the measurement and
  the reasoning.
- [x] 1.4 Add `SDF_ZIGBEE_STEERING_RETRY_MIN_MS` / `_MAX_MS`,
  `s_steering_retry_delay_ms`, `sdf_zigbee_reset_steering_backoff()` and
  `sdf_zigbee_next_steering_delay_ms()`; use them at the retry site and reset on
  both successful join and fresh steering start.
- [x] 1.5 Also cleaned up the second column-0 `esp_zb_ota_upgrade_client_
  query_interval_set()` call on the steering-success path — same botched-edit
  indentation, no behavioral change.
- [x] 1.6 Added `<inttypes.h>` for the `PRIu32` in the retry log line.

## 2. Verification

- [x] 2.1 `idf.py build` clean for `esp32c6`. `sdf.bin` 0x10fe40, 45% free.
- [x] 2.2 Host suite green: 285 Tests, 0 Failures, 11 Ignored — unchanged from
  baseline.
- [x] 2.3 Flashed to the connected esp32c6 and captured 100 s continuously.
  One reset at t=0 (`rst:0x15 (USB_UART_HPSYS)`, the flash tool's own), then no
  further reset, no `Guru Meditation`, no `task_wdt`. Previously: a reboot every
  ~18 s.
- [x] 2.4 Backoff confirmed from the capture: 1000 → 2000 → 4000 → 8000 →
  16000 → 32000 → 60000 ms, holding at the ceiling.
- [x] 2.5 Factory-new path confirmed: `Device started in factory-reset mode`
  (single space) followed by `Start network steering` alone — the contradictory
  `Device rebooted and using existing network` no longer accompanies it.

## 3. Tests — NOT DONE, see 3.3

- [ ] 3.1 Cover the backoff progression and both reset points as a pure
  function of failure count.
- [ ] 3.2 Cover that the non-factory-new startup path marks the device joined.
- [x] 3.3 **Decision: 3.1-3.2 are not covered by automated tests.**
  `sdf_protocol_zigbee` is absent from the linux test runner's component list
  (`firmware/test_runner/CMakeLists.txt`) — it depends on `esp-zigbee-lib`,
  which is closed source and has no linux-target build, and the whole file is
  inside `#ifndef CONFIG_IDF_TARGET_LINUX`. This is the same structural gap
  recorded for `sdf_ble_companion` in `fix-ble-bond-seed-init-order` task 4.4.
  The backoff helpers are pure and would be testable if extracted behind a seam;
  that extraction is a separate decision, not part of this fix. **Real coverage
  gap: a future edit could reintroduce either defect and nothing would catch it
  before flashing.** Tracked as 4.2.

## 4. Follow-on, out of scope here

- [ ] 4.1 **Not verified: reboot onto an existing network.** This board is
  factory-new, so it steers on every boot and the restored `else` branch never
  executed during the capture. It needs a board commissioned into a real Zigbee
  network, rebooted, and observed to report lock state afterwards. This is the
  branch whose absence caused the field-visible failure, so it is the one most
  worth confirming.
- [ ] 4.2 Close the coverage gap from 3.3, together with the equivalent gap in
  `fix-ble-bond-seed-init-order` 4.4/6.3 — both are the same problem: components
  that cannot build for the linux target have no regression net at all.
- [ ] 4.3 Audit the tree for other column-0 statements inside indented blocks.
  Two independent instances of the same botched-edit signature turned up in one
  file; a `clang-format --dry-run` gate in `firmware-ci.yml` would have caught
  both mechanically.
- [ ] 4.4 Revisit whether `esp-zigbee-lib`'s scan can be made to yield — an
  Espressif-side question. If it ever can, watching idle again becomes viable.
