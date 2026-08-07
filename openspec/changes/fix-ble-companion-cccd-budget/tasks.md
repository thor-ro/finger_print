## 1. Kconfig fix

- [x] 1.1 Confirm `CONFIG_BT_NIMBLE_MAX_CCCDS` has a genuine Kconfig prompt (not a promptless/derived symbol) by checking `components/bt/host/nimble/Kconfig.in` in the ESP-IDF v6.0.2 tree — confirmed: `config BT_NIMBLE_MAX_CCCDS` / `int "Maximum number of CCC descriptors to save across reboots"` / `default 8` / `depends on BT_NIMBLE_ENABLED`, a genuine integer Kconfig symbol
- [x] 1.2 Set `CONFIG_BT_NIMBLE_MAX_CCCDS=16` explicitly in `firmware/sdkconfig.defaults`
- [x] 1.3 Delete `firmware/sdkconfig` and regenerate via `idf.py set-target esp32c6` so the new default is reconciled into the generated config (hand-editing the generated `sdkconfig` alone is not sufficient)

## 2. Verification

- [x] 2.1 Run a clean `idf.py build` in `firmware/` and confirm it succeeds — build completed successfully (`sdf.bin` generated, 45% app partition free)
- [x] 2.2 Confirm `firmware/sdkconfig` reflects `CONFIG_BT_NIMBLE_MAX_CCCDS=16` via `grep` — confirmed at `firmware/sdkconfig:1250`

## 3. Spec sync

- [x] 3.1 Merge the `ble-companion-service` delta spec (new "Persisted Notification Subscription Capacity" requirement) into `openspec/specs/ble-companion-service/spec.md`
