## 1. Kconfig option

- [x] 1.1 Add `CONFIG_SDF_CLI_ENABLE` (bool, default `n`) to `components/sdf_config/Kconfig`'s existing "CLI" menu (repo convention consolidates all SDF Kconfig options there; `components/sdf_cli/Kconfig` stays a placeholder like every other component's)
- [x] 1.2 Set `CONFIG_SDF_CLI_ENABLE=y` in `firmware/sdkconfig.debug.defaults`
- [x] 1.3 Set `CONFIG_SDF_CLI_ENABLE=n` in `firmware/sdkconfig.release.defaults` (base `sdkconfig.defaults` already gets `n` from the Kconfig option's own default, so it doesn't need an explicit override)

## 2. Gate the build

- [x] 2.1 **Revised from the original design.** `REQUIRES`/`PRIV_REQUIRES` can't depend on `CONFIG_` values - they're expanded in a separate early CMake pass before Kconfig is loaded (confirmed against ESP-IDF's own `api-guides/build-system.rst:468`, and reproduced as a real build failure: `sdf_app` conditionally requiring `sdf_cli` silently lost that edge because the early pass sees `CONFIG_SDF_CLI_ENABLE` as unset). Fixed by keeping `sdf_app`'s `REQUIRES sdf_cli` unconditional, and instead making `components/sdf_cli/CMakeLists.txt`'s `SRCS` conditional: `sdf_cli.c` is always built, `sdf_cli_commands.c` (the actual esp_console/argtable3/linenoise-backed commands) only when `CONFIG_SDF_CLI_ENABLE` is set.
- [x] 2.2 **Revised from the original design.** Since `sdf_app.c` must call `sdf_cli_init()` and include `sdf_cli.h` unconditionally now (see 2.1), the gating moved inside `sdf_cli.c` itself: its real implementation is wrapped in `#ifdef CONFIG_SDF_CLI_ENABLE`, with an `#else` branch providing no-op stubs for all 5 public API functions (`sdf_cli_init` returns `ESP_OK`, `sdf_cli_is_authenticated` returns `false`, the rest are empty). `sdf_app.c`'s call site is unconditional and unchanged from before this feature existed.
- [x] 2.3 Grep for any other `sdf_cli_*` symbol references outside `sdf_app.c` and `sdf_cli`'s own sources/tests — confirmed `sdf_app.c` is the only external caller. No guarding needed there since the stub keeps the API always callable.

## 3. Verify both configurations build

- [x] 3.1 Build with `sdkconfig.debug.defaults` (CLI enabled) — clean link, `sdf.elf` contains `sdf_cli_init` plus `esp_console_cmd_register`/`linenoise` symbols (34 refs). Build fails only at the partition-size-check step (binary too large for the OTA partition) - reproduced the identical failure against an unmodified `HEAD` checkout in an isolated worktree with the same sdkconfig, confirming it's the pre-existing flash-pressure problem this whole effort is about, not a regression from this change.
- [x] 3.2 Build with `sdkconfig.release.defaults` (CLI disabled) — clean build **and** clean link, including the partition-size check (10% flash free). `nm` on `sdf.elf` shows zero `esp_console`/`linenoise`/`argtable` symbols; the only remaining `sdf_cli_*` symbol is the stub `sdf_cli_init`.
- [x] 3.3 Measured via an isolated A/B (`sdkconfig.defaults` alone, only `CONFIG_SDF_CLI_ENABLE` flipped, so profile-level differences like optimization/log level don't muddy the comparison) and `idf_size.py --archives`:
  - `libconsole.a`: 14,280 B (present when enabled, absent when disabled)
  - `libsdf_cli.a`: 5,879 B enabled vs. 4 B disabled (just the stub)
  - Flash Code total: 1,743,870 B enabled vs. 1,709,234 B disabled → **34,636 B saved**
  - Final `sdf.bin`: 1,904,480 B enabled vs. 1,869,760 B disabled → **34,720 B (~33.9 KB) saved**

## 4. Tests and docs

- [x] 4.1 `components/sdf_cli/test/test_sdf_cli.c` is unaffected: it's part of `sdf_cli`'s own `test/` sources, built directly against `sdf_cli.c`/`sdf_cli_commands.c` by `test_runner`'s own component registration, independent of `CONFIG_SDF_CLI_ENABLE` (that flag only affects how `sdf_app`'s production build pulls in `sdf_cli`, not how `test_runner` builds `sdf_cli` for its own tests).
- [x] 4.2 Documented `CONFIG_SDF_CLI_ENABLE` in `components/sdf_config/Kconfig`'s help text (see 1.1) and in this change's `proposal.md`/`design.md`. No other CLI usage docs exist in the repo to update.
