## Why

`sdf_ble_companion` registers 4 NOTIFY-capable GATT characteristics (auth, config, enroll, ota — see `s_characteristics[]` in `sdf_ble_companion.c`), and `sdkconfig.defaults` allows up to `CONFIG_BT_NIMBLE_MAX_BONDS=3` bonded peers. Each bonded peer can independently persist a CCCD (notification subscription) per characteristic, so worst-case persisted-CCCD demand is `4 × 3 = 12`. The current `CONFIG_BT_NIMBLE_MAX_CCCDS=8` is already below that worst case in production, independent of any other change. If the CCCD table fills, NimBLE fails to persist a subscription (`ble_store_write` for a CCCD record fails once the table is full), so a bonded peer's notify subscription for one of the four characteristics can silently fail to survive a reconnect — most likely to surface with 3 bonded peers all subscribed to every characteristic.

## What Changes

- Raise `CONFIG_BT_NIMBLE_MAX_CCCDS` in `firmware/sdkconfig.defaults` from 8 to 16, giving headroom above the worst-case demand of 12 (current 4 characteristics × 3 bonds) for one additional NOTIFY characteristic without exhausting the budget again.
- Regenerate `firmware/sdkconfig` from the updated defaults and verify a clean build.
- Add an explicit requirement to the `ble-companion-service` spec that the persisted CCCD capacity SHALL cover the worst-case bonded-peer × NOTIFY-characteristic product, so future additions of NOTIFY characteristics or bonded-peer capacity are checked against this budget instead of silently regressing it.

## Capabilities

### New Capabilities
(none)

### Modified Capabilities
- `ble-companion-service`: adds a requirement that persisted notification-subscription (CCCD) capacity SHALL be sized to cover all bonded peers subscribing to all NOTIFY-capable characteristics, currently satisfied by raising `CONFIG_BT_NIMBLE_MAX_CCCDS` to 16.

## Impact

- `firmware/sdkconfig.defaults` — `CONFIG_BT_NIMBLE_MAX_CCCDS` raised from 8 (implicit default) to 16.
- `firmware/sdkconfig` — regenerated to reflect the new default.
- `openspec/specs/ble-companion-service/spec.md` — new requirement documenting the CCCD capacity guarantee.
- No source code changes; no new characteristics, no behavioral change to the GATT protocol itself.
- RAM/NVS cost: each persisted CCCD record (`struct ble_store_value_cccd`) is ~16 bytes; raising the table from 8 to 16 entries adds roughly 128 bytes of NVS-backed storage — negligible against the flash/RAM budget.
