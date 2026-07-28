## Context

Current state: Configuration defaults are scattered across 4 locations:
- `sdf_power_get_default_power_config()` - 12 Kconfig values
- `sdf_services_get_default_config()` - 18 Kconfig values  
- Inline in `sdf_app_init()` - 20+ Kconfig values
- `sdf_config_get_defaults()` - supposed single source but incomplete

This causes drift risk, testing difficulty, unused runtime override, and validation gaps.

## Goals / Non-Goals

**Goals:**
1. `sdf_config` becomes single source of truth for ALL runtime configuration
2. Remove duplicate `get_default_*_config()` functions from 3 components
3. Enable `sdf_config_get_mutable()` for runtime reconfiguration
4. Centralize all validation in `sdf_config_validate()`
5. Consolidate Kconfig under unified menu structure

**Non-Goals:**
- Change Kconfig key names (breaking change - separate migration)
- Add new configuration capabilities
- Modify component APIs (only internal initialization changes)

## Decisions

### Decision 1: Struct Organization - Flat vs Nested

**Option A: Flat struct** (current proposal) - All fields at top level with comment sections
```c
typedef struct {
    // Power
    uint32_t checkin_interval_ms;
    // Services
    uint32_t match_poll_interval_ms;
    // ...
} sdf_config_t;
```

**Option B: Nested structs** - Group by domain
```c
typedef struct {
    struct {
        uint32_t checkin_interval_ms;
        // ...
    } power;
    struct {
        uint32_t match_poll_interval_ms;
        // ...
    } services;
    // ...
} sdf_config_t;
```

**Decision: Option A (Flat)** - Matches existing `sdf_config_t` style, simpler access via `cfg->field`, easier migration for existing code. Comment sections provide logical grouping.

**Open Thread: Field naming consistency**
- Current mix: `fp_wake_gpio` vs `wake_gpio` vs `power_en_gpio`
- Power uses `fp_wake_gpio`, Services uses `wake_gpio` + `power_en_gpio`
- **Question**: Standardize to `fp_wake_gpio`, `fp_power_en_gpio` everywhere? Or keep component-specific names?

### Decision 2: Kconfig Menu Structure

Current keys scattered across component Kconfigs. Proposal unifies under `menu "SDF Configuration"`.

**Decision: Keep component Kconfig files** but reorganize menus:
- `sdf_power/Kconfig` → defines Power Management menu
- `sdf_services/Kconfig` → defines Fingerprint Services menu  
- `sdf_protocol_zigbee/Kconfig` → defines Zigbee menu
- New `sdf_config/Kconfig` → top-level menu including all submenus + Security + Nuki BLE

**Open Thread: Nuki BLE config location**
- Currently only in `sdkconfig.defaults` and `sdf_app.c` inline
- No dedicated Kconfig
- **Question**: Add to `sdf_protocol_ble/Kconfig` or create new `sdf_config` entries?

### Decision 3: Migration Strategy - Additive vs Big Bang

**Option A: Additive (Recommended)**
1. Add all fields to `sdf_config_t` (new fields alongside existing)
2. Implement `get_defaults()` mapping
3. Add validation
4. Switch components one at a time
5. Remove old functions

**Option B: Big Bang**
- All changes in single PR
- Higher risk, harder to bisect

**Decision: Option A (Additive)** - Lower risk, incremental validation.

### Decision 4: Runtime Override API

` sdf_config_get_mutable()` exists but unused. Two patterns:

**Pattern A: Direct mutation**
```c
sdf_config_t *cfg = sdf_config_get_mutable();
if (cfg) cfg->checkin_interval_ms = 30000;
```

**Pattern B: Setter functions**
```c
esp_err_t sdf_config_set_checkin_interval(uint32_t ms);
```

**Decision: Pattern A (Direct)** - Simpler, matches existing `get_mutable()` design. Add setter functions only if validation needed at set time.

**Open Thread: Thread safety**
- `sdf_config_t` is global mutable state
- Multiple tasks could call `get_mutable()`
- **Question**: Add mutex to `sdf_config`? Or document "call at init only"?

### Decision 5: Validation Timing

**Option A: At `sdf_config_init()` only** (current)
- Validates once at boot
- Misses runtime changes via `get_mutable()`

**Option B: On every `get_mutable()` write**
- Requires setter functions or observer pattern

**Decision: Option A** - Document that runtime changes bypass validation; use setters for validated changes.

### Decision 6: Component Mapping Layer

How components map `sdf_config_t` → their internal config structs:

**Option A: Inline mapping in each component init** (current proposal)
```c
// sdf_power.c
const sdf_config_t *cfg = sdf_config_get();
sdf_power_manager_config_t pwr_cfg = {
    .checkin_interval_ms = cfg->checkin_interval_ms,
    // ...
};
```

**Option B: Helper functions in `sdf_config`**
```c
// sdf_config.h
void sdf_config_fill_power_config(sdf_power_manager_config_t *out);
```

**Decision: Option A (Inline)** - Keeps components loosely coupled; no circular deps. Mapping is simple field copy.

### Decision 7: Default Value Source

Kconfig values → `sdf_config_get_defaults()` → component init.

**Open Thread: Compile-time vs runtime defaults**
- `sdf_config_get_defaults()` reads `CONFIG_*` macros at runtime
- Could generate `static const sdf_config_t sdf_config_defaults = { ... }` at build time
- **Question**: Generate defaults at CMake configure time? Avoids Kconfig header includes in components.

## Risks / Trade-offs

| Risk | Likelihood | Impact | Mitigation |
|------|------------|--------|------------|
| Large struct (~80 fields) increases binary size | Medium | Low | Group by domain in comments; compiler optimizes unused |
| Kconfig rename breaks existing `sdkconfig` | High | High | Provide `migrate_sdkconfig.py` script |
| Component init order dependency | Medium | Medium | `sdf_config_init()` called first in `sdf_app_init()` |
| Runtime mutation without validation | Medium | Medium | Document clearly; add setters for critical params |
| Circular dependency: `sdf_config` → components → `sdf_config` | Low | High | Components only read, never write global config |

## Migration Plan

### Phase 1: Extend `sdf_config` (Additive)
1. Add all 50+ fields to `sdf_config_t` in `sdf_config.h`
2. Implement `sdf_config_get_defaults()` mapping in `sdf_config.c`
3. Add validation rules for new fields in `sdf_config_validate()`
4. Extend `sdf_config_dump()` to print all domains
5. Unit test: validate default config passes

### Phase 2: Switch Components (One at a time)
1. **sdf_power**: Replace `sdf_power_get_default_power_config()` → `sdf_config_get()`
2. **sdf_services**: Replace `sdf_services_get_default_config()` → `sdf_config_get()`
3. **sdf_app**: Remove inline Kconfig references → `sdf_config_get()`
4. Test each component independently

### Phase 3: Cleanup
1. Remove old `get_default_*_config()` functions
2. Consolidate Kconfig menus
3. Update documentation

### Phase 4: Validation
1. Enable `CONFIG_SDF_CONFIG_VALIDATE_AT_BOOT=y`
2. Test runtime override via CLI
3. Full integration test

**Rollback**: Each phase commits independently; revert single component if issues.

## Open Questions

1. **Field naming**: Standardize `fp_wake_gpio` / `fp_power_en_gpio` across power and services?
2. **Nuki BLE Kconfig**: Where to define - `sdf_protocol_ble/Kconfig` or `sdf_config/Kconfig`?
3. **Thread safety**: Add mutex to `sdf_config` for `get_mutable()`?
4. **Defaults generation**: Build-time `sdf_config_defaults` struct vs runtime Kconfig reads?
5. **Validation on mutation**: Add setter functions for critical params?
6. **Deprecation timeline**: How long to keep old functions with deprecation warnings?
7. **Migration script**: Auto-generate `sdkconfig` migration from old→new Kconfig keys?
8. **Documentation sync**: Update `sdf_sas.md` §8, §15 and AGENTS.md component list?