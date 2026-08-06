## Context

`sdf_cli` is a password-gated UART/USB-JTAG debug console (`components/sdf_cli`) built on `esp_console` + `argtable3` + `linenoise`. It is unconditionally listed in `sdf_app`'s `app_reqs` (`components/sdf_app/CMakeLists.txt`) and initialized unconditionally from `sdf_app_init()` (`sdf_cli_init()` call in `components/sdf_app/src/sdf_app.c`). `components/sdf_cli/Kconfig` currently contains only a comment ("Kconfig entries consolidated into sdf_config/Kconfig") — there is no existing option controlling whether the component is built at all. There are three sdkconfig profiles: `sdkconfig.defaults` (base/production defaults), `sdkconfig.debug.defaults`, and `sdkconfig.release.defaults`.

## Goals / Non-Goals

**Goals:**
- Make `sdf_cli` compile-time optional via a single Kconfig option.
- Default it on for debug builds, off for release/production builds, with zero behavior change to the CLI itself when enabled.
- Keep `sdf_app` building cleanly in both configurations.

**Non-Goals:**
- No change to `sdf_cli`'s authenticated command set, password handling, or idle-timeout behavior.
- No runtime (post-flash) toggle — this is a build-time-only option, consistent with how other SDF_* Kconfig knobs in this repo work.

## Decisions

- **New option name/placement**: `CONFIG_SDF_CLI_ENABLE`, defined in `components/sdf_config/Kconfig`'s existing `menu "CLI"` (which already holds `SDF_CLI_PASSWORD`) rather than `components/sdf_cli/Kconfig`. **Revised during implementation**: the repo has an established, consistent convention of consolidating every `SDF_*` Kconfig option into `sdf_config/Kconfig`, with individual components' own `Kconfig` files left as placeholder comments pointing there — confirmed against `sdf_platform/Kconfig`, `sdf_protocol_ble/Kconfig`, and `sdf_cli/Kconfig` itself. Default `n` (see per-profile defaults below).
  - *Alternative considered*: a `CONFIG_SDF_BUILD_PROFILE` (debug/release) master switch controlling several such options at once. Rejected for this change — no other component currently needs profile-gating, so a single-purpose option is simpler and avoids inventing an abstraction with only one user.
- **Gating point**: **Revised during implementation.** The original plan — conditionally appending `sdf_cli` to `sdf_app`'s `REQUIRES` based on `CONFIG_SDF_CLI_ENABLE` — turned out to be unsupported by ESP-IDF's build system: `REQUIRES`/`PRIV_REQUIRES` are expanded in a separate early CMake pass (`component_get_requirements.cmake`, run via `execute_process` before `sdkconfig.cmake` is loaded), so `if(CONFIG_...)` around them silently evaluates false regardless of the real config value (confirmed against `api-guides/build-system.rst:468`: *"values REQUIRES PRIV_REQUIRES should not depend on any configuration options ... because requirements are expanded before configuration loaded"*). This was caught by reproducing the exact failure — `sdf_app.c: fatal error: sdf_cli.h: No such file or directory` despite `CONFIG_SDF_CLI_ENABLE=y` — against a clean checkout of `HEAD` in an isolated worktree.

    The actual gating instead lives inside `sdf_cli`:
    - `sdf_app`'s `REQUIRES sdf_cli` stays unconditional, and its `#include "sdf_cli.h"` / `sdf_cli_init()` call site are unconditional too (unchanged from before this feature).
    - `sdf_cli/CMakeLists.txt` makes `SRCS` conditional instead (source file lists *can* depend on `CONFIG_` values per the same doc) — `sdf_cli.c` always builds, `sdf_cli_commands.c` (the actual `esp_console`/`argtable3`/`linenoise`-backed commands) only builds when `CONFIG_SDF_CLI_ENABLE` is set.
    - `sdf_cli.c` wraps its real implementation in `#ifdef CONFIG_SDF_CLI_ENABLE`, falling back to an `#else` branch of no-op stubs for all 5 public functions declared in `sdf_cli.h`, so the public API is always callable and callers never need their own `#ifdef`.
  - *Alternative considered (superseded)*: keep `sdf_cli` always linked but make `sdf_cli_init()` a runtime no-op when disabled. This is effectively what the revised approach does, except the no-op-ness (and exclusion of `sdf_cli_commands.c`) is compile-time, so `argtable3`/`linenoise`/`esp_console` are never referenced and the linker drops them entirely — confirmed via `nm` showing zero such symbols in a disabled build.
- **Per-profile defaults**: set `CONFIG_SDF_CLI_ENABLE=y` in `sdkconfig.debug.defaults` and `CONFIG_SDF_CLI_ENABLE=n` in `sdkconfig.release.defaults`. The Kconfig option's own default (`n`) covers the base `sdkconfig.defaults` case, so no explicit override is needed there.
- **`test_runner` fallout**: `test_runner`'s `sdkconfig.defaults` didn't set `CONFIG_SDF_CLI_ENABLE`, so it would've defaulted to `n` and silently swapped `test_sdf_cli.c`'s assertions onto the no-op stub instead of the real logic it's meant to exercise. Added `CONFIG_SDF_CLI_ENABLE=y` there too, following that file's existing convention of documenting *why* each override exists.

## Risks / Trade-offs

- [Risk] A developer builds without the debug profile and loses console access unexpectedly, mid-debugging. → Mitigation: document the option in the CLI's usage notes / README and make the "how to enable the debug console" step obvious (`idf.py -DSDKCONFIG_DEFAULTS=sdkconfig.debug.defaults build`, matching existing debug/release profile usage elsewhere in this repo).
- [Risk] `sdf_app.c` accumulates more `#ifdef CONFIG_SDF_CLI_ENABLE` blocks over time if CLI-dependent code is added elsewhere. → Mitigation: keep the guard narrowly scoped to the init call site; anything else needing CLI-awareness should go through a small accessor (e.g. `sdf_cli_is_authenticated()` already exists and is already safe to leave unguarded/declared, since it's cheap even if the .c file compiles to nothing — confirm during implementation whether any other call sites exist).

## Migration Plan

- No data/runtime migration — this is a build-config change. Existing devices in the field are unaffected until reflashed with a release-profile build.
- Rollout: land the Kconfig option with defaults as specified; CI (once `add-firmware-ci` lands) should build both profiles to catch either configuration breaking.
- Rollback: revert the Kconfig default or pass `-DSDKCONFIG_DEFAULTS=sdkconfig.debug.defaults` to restore prior always-on behavior; no code path is deleted, only conditionally compiled.

## Open Questions

- ~~Are there other call sites referencing `sdf_cli_*` symbols outside `sdf_app.c` and `sdf_cli`'s own tests that would need guarding?~~ **Resolved during implementation**: grepped the whole tree — `sdf_app.c` is the only external caller. No other guarding needed.
- Should `sdkconfig.release.defaults` be the one actually used to produce OTA images distributed to devices, or is `sdkconfig.defaults` alone the production build? (Affects which file's default actually matters in practice — confirm against how `add-firmware-ci` builds firmware, once that change lands.)
