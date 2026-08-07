## Context

`sdf_ble_companion` (GATT server on the shared NimBLE host) registers 4 NOTIFY-capable characteristics: auth, config, enroll, ota (`s_characteristics[]` in `sdf_ble_companion.c`). `firmware/sdkconfig.defaults` sets `CONFIG_BT_NIMBLE_MAX_BONDS=3`. NimBLE persists one CCCD (Client Characteristic Configuration Descriptor — the notification-subscription record) per bonded-peer-per-NOTIFY-characteristic in its NVS-backed store (`CONFIG_BT_NIMBLE_NVS_PERSIST=y`, `ble_store_nvs.c`). Worst-case demand is therefore `4 characteristics × 3 bonds = 12` CCCD records, but `CONFIG_BT_NIMBLE_MAX_CCCDS` is currently 8 (the ESP-IDF NimBLE port's built-in default — `firmware/sdkconfig.defaults` did not previously override it). This is a pre-existing production bug, not something introduced by the recent BLE-OTA change, though it was surfaced while auditing GATT characteristics during that work (see `.serena/memories/ble-companion-nimble-cccd-budget.md`).

Confirmed `CONFIG_BT_NIMBLE_MAX_CCCDS` has a real Kconfig prompt (unlike the promptless `CONFIG_ESP_WIFI_ENABLED` symbol found during the BLE-OTA change): `components/bt/host/nimble/Kconfig.in` declares `config BT_NIMBLE_MAX_CCCDS` / `int "Maximum number of CCC descriptors to save across reboots"` / `default 8` / `depends on BT_NIMBLE_ENABLED`, so it is a genuine integer Kconfig symbol that `sdkconfig.defaults` can override.

## Goals / Non-Goals

**Goals:**
- Raise the persisted-CCCD budget to comfortably cover the current worst case (12) plus headroom for one more NOTIFY characteristic without a repeat of this bug.
- Make the capacity relationship (bonds × NOTIFY characteristics ≤ CCCD budget) an explicit, checkable requirement so it isn't silently violated again.

**Non-Goals:**
- No change to the number of NOTIFY-capable characteristics, `CONFIG_BT_NIMBLE_MAX_BONDS`, or any GATT protocol behavior.
- No change to which characteristics are NOTIFY-capable or how subscriptions are used at runtime.

## Decisions

- **Raise `CONFIG_BT_NIMBLE_MAX_CCCDS` from 8 to 16** (set explicitly in `firmware/sdkconfig.defaults`), rather than reducing `CONFIG_BT_NIMBLE_MAX_BONDS` or restructuring characteristics to share notify slots.
  - Alternatives considered:
    - *Reduce `CONFIG_BT_NIMBLE_MAX_BONDS`*: would shrink the product below 8, but reduces the number of companion devices/users that can stay bonded simultaneously — a functional regression, not a config-hygiene fix.
    - *Collapse NOTIFY characteristics (opcode-multiplex over fewer characteristics)*: the BLE-OTA change already did this for the OTA data path; doing it further for auth/config/enroll would be a larger, riskier protocol change disproportionate to a Kconfig-budget bug.
    - *Raise to exactly 12 (the current worst case)*: leaves zero headroom — any future NOTIFY characteristic addition (there is prior-art precedent: the BLE-OTA change reused an existing characteristic specifically to avoid this) would immediately reopen the bug.
  - 16 was chosen as the smallest power-of-two above 12 that leaves room for one additional NOTIFY characteristic (`4 × 4 = 16`) at the current bond limit, while the RAM/NVS cost stays negligible: each `struct ble_store_value_cccd` record is ~16 bytes (peer address + characteristic handle + flags), so the table grows by roughly 128 bytes.
- **Regenerate `firmware/sdkconfig` from `sdkconfig.defaults`** via `idf.py set-target esp32c6` rather than hand-editing the generated `sdkconfig`. A prior session in this repo confirmed that editing `sdkconfig.defaults` alone does not reconcile an already-generated `sdkconfig`; the generated file must be deleted and regenerated (or reconciled through the IDF tool) for the new default to actually take effect in the build.
- **Add a spec-level requirement** to `ble-companion-service` stating persisted-CCCD capacity SHALL cover the worst-case bonded-peer × NOTIFY-characteristic product. This is the one piece of this change that is genuinely requirement-level (a system guarantee, not just an implementation detail): it gives future changes that add a NOTIFY characteristic or raise `CONFIG_BT_NIMBLE_MAX_BONDS` a concrete, checkable constraint instead of relying on tribal knowledge in a memory file.

## Risks / Trade-offs

- [Future NOTIFY characteristic additions could still exceed the new budget] → Mitigated by the new spec requirement making the capacity relationship explicit and reviewable; the `.serena/memories/ble-companion-nimble-cccd-budget.md` note already flags this for future GATT changes.
- [Marginally increased NVS footprint for the CCCD table] → Negligible (~128 bytes), and flash headroom is not a current constraint after the BLE-OTA WiFi-stack removal.

## Migration Plan

1. Set `CONFIG_BT_NIMBLE_MAX_CCCDS=16` in `firmware/sdkconfig.defaults`.
2. Delete `firmware/sdkconfig` and regenerate via `idf.py set-target esp32c6` (or equivalent) so the new default is actually reconciled into the generated config.
3. Run a clean `idf.py build` and confirm `firmware/sdkconfig` reflects `CONFIG_BT_NIMBLE_MAX_CCCDS=16`.
4. No data migration needed: existing bonded peers' already-persisted CCCD records are unaffected; the change only raises the ceiling for future subscriptions.

## Open Questions

None.
