## 1. Add Timer Infrastructure

- [ ] 1.1 In `sdf_ble_companion.c`, add `static esp_timer_handle_t s_adv_timer` and a constant `#define SDF_BLE_COMPANION_ADV_FAST_DURATION_MS 30000`
- [ ] 1.2 Create `sdf_ble_companion_adv_timer_cb(void *arg)`: stop current advertising, restart with slow interval params
- [ ] 1.3 Create `sdf_ble_companion_start_advertising_fast()`: start advertising with fast interval, then arm `s_adv_timer` for `SDF_BLE_COMPANION_ADV_FAST_DURATION_MS` via `esp_timer_start_once()`
- [ ] 1.4 Create `sdf_ble_companion_start_advertising_slow()`: start advertising with slow interval (`BLE_GAP_ADV_SLOW_INTERVAL1_MIN/MAX`)

## 2. Wire Up Fast/Slow Switching

- [ ] 2.1 Replace `sdf_ble_companion_start_advertising()` calls with `sdf_ble_companion_start_advertising_fast()`
- [ ] 2.2 In `BLE_GAP_EVENT_DISCONNECT` handler: cancel and restart the fast timer by calling `sdf_ble_companion_start_advertising_fast()` again
- [ ] 2.3 In `sdf_ble_companion_init()`: initialize the esp_timer but do not start it yet
- [ ] 2.4 In `sdf_ble_companion_deinit()`: stop and delete the esp_timer

## 3. Build & Verify

- [ ] 3.1 Build firmware, confirm no compile errors
- [ ] 3.2 Verify that after 30s, advertising interval in a BLE scanner increases (slower beacon rate)
