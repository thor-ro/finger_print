# Pattern: extracting a host-testable pure module from a NimBLE-dependent component

When a component's `.c` file depends on NimBLE headers (`host/ble_hs.h`, etc.) and
therefore cannot be added wholesale to `firmware/test_runner` (linux host target,
since `bt`/NimBLE isn't available there), the established way to still get unit
test coverage for its wire-format validation / pure-decision logic is to factor
that logic into a **separate, dependency-free sibling module** within the same
component:

- `include/<name>_protocol.h` + `src/<name>_protocol.c` — no NimBLE, no
  `sdf_ota`/esp_log/etc includes, only stdlib (`<string.h>`, `<stdint.h>`,
  `<stdbool.h>`). Pure functions taking primitive args, returning bool/enum.
- The owning component's real `.c` file (e.g. `sdf_ble_companion_ota.c`) includes
  this header and delegates validation/decision logic to it, keeping its own
  body focused on I/O, locking, and NimBLE calls.
- Add the new `_protocol.c` to the owning component's `CMakeLists.txt` `SRCS`.
- In `firmware/test_runner/main/CMakeLists.txt`, directly list the new
  `_protocol.c` + a new `test/test_<name>_protocol.c` in `SRCS` (mirroring the
  pre-existing `sdf_nuki_crypto.c` + `test_nuki_crypto.c` precedent), and add
  the owning component's `include/` dir to `INCLUDE_DIRS` (do NOT add the whole
  component to `REQUIRES` — that would try to build its NimBLE-dependent `.c`
  files for the linux target too, which fails).
- Test function prototypes must be forward-declared with `extern void
  test_xxx(void);` near the top of `test_runner_main.c` (no shared header used
  in this codebase for that) and registered with `RUN_TEST(...)` in
  `app_main()`, exactly like all other suites in that file.
- Build order: `source <idf>/export.sh` then
  `idf.py -B build_linux -D IDF_TARGET=linux build` from `firmware/test_runner/`;
  run `./build_linux/sdf_test_runner.elf` directly, grep its Unity output.

Concrete example added by `replace-wifi-ota-with-ble-transfer`:
`firmware/components/sdf_ble_companion/include/sdf_ble_ota_protocol.h` +
`src/sdf_ble_ota_protocol.c`, tested via
`firmware/components/sdf_ble_companion/test/test_sdf_ble_ota_protocol.c`.
This is the template to reuse for any future NimBLE-adjacent pure-logic that
needs host test coverage (idle-timeout and signature-failure paths still
remain HIL-only, consistent with existing Zigbee/signature test precedent).
