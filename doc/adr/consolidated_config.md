# ADR 002: Consolidate Configuration into sdf_config
Date: 2026-07-29
Status: Proposed

## 1. Context and Problem Statement
The SDF firmware has system configuration scattered across 8 Kconfig files (one per component), hardcoded defaults in `sdf_config.c`, and 6 components that read `CONFIG_SDF_*` macros directly instead of using the `sdf_config_t` runtime struct. This makes configuration hard to discover, understand, or modify. Five GPIO/pin assignments (`fp_tx_pin`, `fp_rx_pin`, `fp_uart_port`, `fp_baud_rate`, `battery_adc_pin`) have no Kconfig entry at all — they live as hardcoded C literals invisible to `menuconfig`.

## 2. Considered Options

### Option A: Flatten all Kconfig into per-component files
Keep each component's Kconfig as-is. Add missing entries to the relevant component's Kconfig file. This preserves the existing ESP-IDF convention but doesn't solve discoverability.

### Option B: Create a single master Kconfig in sdf_config
Consolidate all 8 per-component Kconfig files into one `sdf_config/Kconfig` with submenus for organization. Add missing config entries for hardcoded values. This provides a single place to see all options.

### Option C: Remove Kconfig entirely, use a C config header
Replace Kconfig with a single `sdf_config_defaults.h` header containing `#define` constants. All components include this header. This is simpler but loses ESP-IDF integration (`menuconfig`, `sdkconfig`, build system defaults).

## 3. Decision
We will use **Option B**: consolidate into `sdf_config/Kconfig` with submenus and make `sdf_config_t` the single runtime access path.

## 4. Rationale
Option B preserves ESP-IDF conventions (`menuconfig`, `sdkconfig.defaults`, `CONFIG_SDF_*` macros) while closing the gap between build-time config and runtime config. Option A doesn't solve discoverability. Option C loses valuable ESP-IDF tooling and the separation between compile-time feature flags and runtime values.

The consolidation specifically closes three gaps:
1. **Missing Kconfig entries**: 5 hardcoded GPIO/pin values get Kconfig options
2. **Hardcoded defaults in C**: `sdf_config.c` will pull all defaults from Kconfig, not literals
3. **Direct Kconfig reads**: 6 components will route runtime config through `sdf_config_get()` instead of reading `CONFIG_SDF_*` directly

## 5. Consequences
**Positive:**
- Single source of truth for all system configuration options
- All GPIO/pin mappings visible and editable in `menuconfig`
- No hidden hardcoded defaults — everything is Kconfig-driven
- Consistent runtime access via `sdf_config_get()` across all components
- Compile-time feature flags (`#if CONFIG_SDF_OTA_*`) remain as Kconfig directives (correct usage)

**Negative:**
- `menuconfig` navigation hierarchy changes (from 8 component menus to 1 SDF Config menu with submenus)
- 6 component source files need runtime read-path updates
- Per-component Kconfig files become empty or are removed (potential confusion if someone adds a new component and expects a new Kconfig)
- Slight increase in `sdf_config/Kconfig` size (single large file vs. 8 small ones)