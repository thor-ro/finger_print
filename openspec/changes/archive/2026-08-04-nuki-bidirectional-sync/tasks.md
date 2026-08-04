## 1. Extend Power Wake Path

- [ ] 1.1 In `sdf_app.c`, add a static flag `s_periodic_poll_pending` (bool)
- [ ] 1.2 In `sdf_app_power_wakeup()`: when wake reason is TIMER and `!sdf_nuki_ble_is_ready(&s_ble)`, set `s_periodic_poll_pending = true` and call `sdf_app_resume_ble_transport("periodic state poll")`
- [ ] 1.3 In `sdf_app_on_ble_ready()`: after the lock-action-pending check, if `s_periodic_poll_pending`, clear the flag and call `sdf_app_request_keyturner_state()`

## 2. Add Config Option

- [ ] 2.1 In `sdf_config.h` / `sdf_config.c`, add `nuki_state_poll_interval_ms` config field (default: same as `checkin_interval_ms`, i.e., 15000ms)
- [ ] 2.2 In `firmware/sdkconfig.defaults`, add a Kconfig entry for the poll interval

## 3. Verify

- [ ] 3.1 Build firmware, confirm no compile errors
- [ ] 3.2 Verify that after a wake-from-timer cycle, `sdf_app_request_keyturner_state()` is called and Zigbee lock state is updated (check logs)
