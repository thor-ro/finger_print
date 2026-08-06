## Why

The ESP32-C6 app partition is 92% full (1,870.5 KB used of 1,964.0 KB in `ota_1`, ~93.5 KB / 4.8% free). `sdf_cli` — a password-gated UART/USB-JTAG debug console — is unconditionally `REQUIRES`'d by `sdf_app` (`components/sdf_app/CMakeLists.txt`), so every production build pays for `esp_console`, `argtable3`, `linenoise`, and `sdf_cli`'s own command table even though the console exists purely to help developers debug hardware in hand. Gating it behind a Kconfig option reclaims that space in release builds without touching the debug workflow.

## What Changes

- Add a new Kconfig option (`CONFIG_SDF_CLI_ENABLE`, in `components/sdf_config/Kconfig`'s "CLI" menu, per this repo's convention of consolidating all `SDF_*` options there) that controls whether the debug-console command set is compiled in at all.
- Default the option to **enabled** on the `sdkconfig.debug.defaults` profile and **disabled** on the release/production profile (`sdkconfig.release.defaults` / default `sdkconfig.defaults`), so `idf.py build` for production naturally excludes it.
- Keep `sdf_cli` unconditionally `REQUIRES`'d by `sdf_app` (ESP-IDF resolves `REQUIRES` before Kconfig is loaded, so it can't itself depend on `CONFIG_*` values) and gate at the source level instead: `sdf_cli/CMakeLists.txt` only compiles `sdf_cli_commands.c` (the `esp_console`/`argtable3`/`linenoise`-backed commands) when the option is set, and `sdf_cli.c` falls back to a no-op stub implementation of its public API when it isn't — so `sdf_app.c`'s call site stays unconditional and `sdf_app` builds cleanly either way.
- **BREAKING** (build-config only, not runtime API): with the option disabled, the UART/USB-JTAG debug console is compiled out entirely — flashing a "release" build removes the ability to reach `sdf_cli` commands until a debug build is reflashed. No impact on lock/fingerprint/BLE/Zigbee functionality.

## Capabilities

### New Capabilities
- `cli-console-build-gating`: Defines the build-time Kconfig option controlling whether the debug UART/USB-JTAG console (`sdf_cli`) is compiled into the firmware, and its default per build profile.

### Modified Capabilities
(none — the CLI's authenticated command behavior, once enabled, is unchanged; no existing spec documents CLI presence/absence)

## Impact

- `components/sdf_config/Kconfig` — add the enable option to the existing "CLI" menu.
- `components/sdf_cli/CMakeLists.txt` — make `sdf_cli_commands.c` a conditional source.
- `components/sdf_cli/sdf_cli.c` — wrap the real implementation in `#ifdef CONFIG_SDF_CLI_ENABLE`, add a no-op stub `#else` branch.
- `firmware/sdkconfig.debug.defaults`, `firmware/sdkconfig.release.defaults` — set the option's default per build profile (base `sdkconfig.defaults` inherits the Kconfig option's own `n` default).
- `firmware/test_runner/sdkconfig.defaults` — explicitly enable the option so `test_sdf_cli.c` exercises the real logic, not the stub.
- Measured savings when disabled (isolated A/B, `idf_size.py --archives`): `libsdf_cli.a` 5,879 B → 4 B, `libconsole.a` 14,280 B removed entirely, Flash Code total −34,636 B, final `sdf.bin` −34,720 B (~33.9 KB).
- No impact on `sdf_ble_companion`, Zigbee, or fingerprint matching code paths.
