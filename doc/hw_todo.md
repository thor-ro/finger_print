# Hardware Verification Todo — power-state-consistency

Run these steps on the ESP32-C6 target after flashing the firmware.

## Prerequisites

- ESP32-C6 board connected via USB serial (e.g., `/dev/cu.Maker4-1405`)
- Fingerprint sensor connected and powered
- Nuki Smart Lock 3 Pro paired and reachable via BLE
- Zigbee network joined (or intentionally left unpaired for fallback test)
- `idf.py` build passing with debug config

## Tasks

- [ ] Flash firmware to hardware: `idf.py -p /dev/cu.Maker4-1405 flash`
- [ ] Verify deep sleep enters after idle timeout (5s default)
  - Trigger a fingerprint match or button press to wake the device
  - Wait 5+ seconds with no activity
  - Confirm device enters deep sleep (monitor UART logs or measure current draw)
- [ ] Verify wake guard prevents immediate re-sleep after wake
  - Wake the device from deep sleep
  - Confirm it stays awake for the post-wake guard period (1.5s default)
  - After guard expires, confirm it re-evaluates sleep and enters deep sleep if idle
- [ ] Verify battery report fires at 60s intervals
  - Wake the device and monitor UART logs
  - Confirm `SDF_EVT_BATTERY_REPORT` is emitted approximately every 60 seconds
- [ ] Verify deep sleep fallback when Zigbee not joined
  - Power on device without Zigbee network joined
  - Confirm it enters deep sleep instead of light sleep when idle
  - Check that `zigbee_ready_cb` returning false triggers `SLEEP_DEEP` decision

## Notes

All verifications should be run after a clean flash (`idf.py erase-flash` before flash).
Use `idf.py monitor` to observe UART output during testing.