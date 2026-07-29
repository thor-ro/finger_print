## Context

The Smart Door firmware has configuration spread across 8 Kconfig files (one per component), a single `sdkconfig.defaults` for defaults, and the `sdf_config_t` runtime struct. The problem is that GPIO/pin mappings and other settings are inconsistently managed:

- 5 GPIO/pin values have **no Kconfig entry** and are hardcoded in `sdf_config.c` (fp_tx_pin=0, fp_rx_pin=1, fp_uart_port=1, fp_baud_rate=19200, battery_adc_pin=5)
- GPIO settings that DO have Kconfig entries live in `sdf_power/Kconfig` even though they're not "power" settings (FP_WAKE_GPIO, FP_EN_GPIO, WS2812_LED_GPIO, ENROLLMENT_BTN_GPIO)
- 6 components read `CONFIG_SDF_*` directly via preprocessor, bypassing the runtime `sdf_config_t` struct, creating two parallel config systems
- `sdkconfig.defaults` is fine as the defaults file, but there's no single place to see all config options

The project uses ESP-IDF's standard Kconfig system, so the fix must preserve that pattern while centralizing.

## Goals / Non-Goals

**Goals:**
- Single source of truth for all system configuration options
- All GPIO/pin mappings visible and configurable via Kconfig
- All configs consumed through `sdf_config_t` at runtime (not direct Kconfig reads)
- `sdkconfig.defaults` remains the canonical defaults file
- Zero hardcoded GPIO/pin values in C source

**Non-Goals:**
- Changing the ESP-IDF Kconfig architecture fundamentally
- Breaking existing `menuconfig` navigation patterns for users
- Adding new runtime config APIs beyond what's needed for route-through

## Decisions

### Decision 1: Consolidate Kconfig into `sdf_config/Kconfig`

Move all entries from 8 component Kconfig files into a single `sdf_config/Kconfig` with submenus for organization. This makes all config options discoverable in one place.

**Alternative considered**: Keep per-component Kconfig files with shared includes. **Rejected** because it doesn't actually solve the discoverability problem — users still need to know which component owns which setting.

### Decision 2: Add Kconfig entries for all hardcoded defaults

Currently hardcoded values (fp_tx_pin, fp_rx_pin, fp_uart_port, fp_baud_rate, battery_adc_pin, and timing defaults like match_poll_interval_ms) get proper Kconfig entries in `sdf_config/Kconfig` with reasonable defaults matching current behavior.

**Alternative considered**: Leave the hardcoded defaults and just document them. **Rejected** because hardcoded values are invisible in `menuconfig` and can't be overridden without editing C source.

### Decision 3: Route direct CONFIG_SDF_* reads through sdf_config_t

Components that currently use `#if CONFIG_SDF_OTA_SIGNATURE_VERIFY` compile-time checks are compile-time decisions (OTA signature verification) and should remain on Kconfig. But components that read numeric/string config values at runtime (power checkin interval, battery report interval, zigbee config) should go through `sdf_config_t`.

**Alternative considered**: Convert all direct reads including compile-time #if guards. **Rejected** because compile-time feature flags (like `#if CONFIG_SDF_OTA_SIGNATURE_VERIFY`) must remain as Kconfig `#if` — they gate code inclusion, not runtime values.

### Decision 4: Keep `sdf_config.h` as the canonical struct

The `sdf_config_t` struct is already the right abstraction. No changes needed to its shape — just ensure it's the single way components access configuration values at runtime.

## Risks / Trade-offs

- [Risk] Removing per-component Kconfig files changes the `menuconfig` navigation hierarchy. **Mitigation**: Use submenus within `sdf_config/Kconfig` to preserve the same grouping (Platform, Power, Security, OTA, etc.)
- [Risk] Direct Kconfig reads in 6 components need refactoring. **Mitigation**: Phase 1 adds Kconfig entries and fixes `sdf_config.c` to pull from them. Phase 2 routes direct reads through `sdf_config_t`.
- [Risk] Some compile-time `#if` guards (e.g., `#if CONFIG_SDF_OTA_SIGNATURE_VERIFY`) must remain as-is — they can't go through `sdf_config_t` since they gate code inclusion. **Mitigation**: These are correctly left in the Kconfig files they logically belong to; the consolidated Kconfig in `sdf_config/Kconfig` will still define these options.
- [Risk] Build system expects Kconfig files in component directories. **Mitigation**: ESP-IDF supports Kconfig in any component directory. Moving all entries to `sdf_config/Kconfig` works because `sdf_config` is always built. The other components' Kconfig files can be removed or emptied.

## Migration Plan

1. Create consolidated `sdf_config/Kconfig` with all current entries plus new ones for hardcoded values
2. Remove or empty per-component Kconfig files
3. Update `sdf_config.c` to pull all hardcoded defaults from Kconfig
4. Update components with direct Kconfig reads to use `sdf_config_t` where runtime
5. Verify build with `idf.py build` and `idf.py menuconfig` shows all options
6. Run existing tests

## Open Questions

- Should the empty Kconfig files be deleted or kept as stubs that include `sdf_config/Kconfig`? (Stubs provide better error messages if someone adds a component-specific option later)
- Should `sdf_config/Kconfig` use `source` directives to include the old per-component Kconfig locations? (This preserves backward compatibility for custom SDK patches)
- What about `CONFIG_IDF_TARGET_LINUX` mock builds — do they need config changes? (Unlikely; they use the same `sdkconfig.defaults`)