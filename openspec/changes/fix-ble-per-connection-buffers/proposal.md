## Why

`s_auth_value`, `s_config_value`, `s_enroll_value`, and `s_ota_value` are single global buffers shared across all 3 possible connections. A write from connection A overwrites what B sees via a Read. When connection B reads the Config characteristic, it gets whatever A last wrote — not its own response. This is particularly dangerous for auth tokens.

## What Changes

- Move the per-characteristic value buffers and lengths into `sdf_ble_companion_connection_t` (one copy per connection)
- Update all access callbacks to use `conn->value_buf` instead of the global `s_*_value` buffers
- Update notify functions to use the connection's own buffer

## Capabilities

### New Capabilities
- None

### Modified Capabilities
- None

## Impact

- `firmware/components/sdf_ble_companion/include/sdf_ble_companion.h` — add buffer fields to `sdf_ble_companion_connection_t`
- `firmware/components/sdf_ble_companion/src/sdf_ble_companion.c` — rework buffer references
- RAM increase: 3 × (512 × 4 buffers) → same total but distributed (net zero; previously 4 × 512 = 2048 bytes global, now 3 × 4 × 512 = 6144 bytes in connection structs)
