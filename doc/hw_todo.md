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

---

# Hardware Verification Todo — fp-power-optimizations

Run these steps on the ESP32-C6 target after flashing the firmware with 115200 baud fingerprint UART.

## Prerequisites

- ESP32-C6 board connected via USB serial (e.g., `/dev/cu.Maker4-1405`)
- Fingerprint sensor connected and powered (verify TX/RX and power enable GPIO wiring)
- `idf.py` build passing with debug config: `idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.debug.defaults" build`

## Tasks

- [ ] Flash firmware to hardware: `idf.py -p /dev/cu.Maker4-1405 flash`
- [ ] Verify 115200 baud UART communication is stable
  - Power on device and monitor UART logs
  - Confirm `Fingerprint initialized (port=..., baud=115200, ...)` log appears
  - Run `fp_probe` (triggered automatically on boot) - confirm "Sensor probe OK" logs
  - Verify no UART framing errors or timeouts during match/enroll operations
- [ ] Verify interrupt-driven match task wake (no continuous polling)
  - Power on device and let it settle (enrolled user required)
  - Monitor UART logs while device is idle (no finger on sensor)
  - Confirm NO periodic "Match request received" or "WDT reset" logs at 400ms intervals
  - Place finger on sensor - confirm immediate match cycle starts (WAKE interrupt triggered)
  - Verify `Match request received` log appears immediately on finger placement
- [ ] Verify fingerprint match works reliably at 115200 baud
  - Enroll a new admin finger (short press button, 3 touches)
  - Test 10 consecutive matches with enrolled finger - confirm all succeed
  - Test 5 consecutive mismatches with unenrolled finger - confirm `NO_MATCH` returned
  - Verify no `TIMEOUT` or `PROTOCOL_ERROR` results during normal operation
- [ ] Verify enrollment works at 115200 baud
  - Start admin enrollment (short press button)
  - Complete 3 enrollment steps - confirm green LED after each
  - Verify enrollment completes and new user can unlock
- [ ] Measure active time reduction (optional, requires current probe)
  - Measure active current draw duration for single match at 19200 vs 115200
  - Confirm ~6x reduction in UART transaction time
  - Verify faster return to light/deep sleep after match

## Notes

All verifications should be run after a clean flash (`idf.py erase-flash` before flash).
Use `idf.py monitor` to observe UART output during testing.

For the interrupt wake test: the WAKE GPIO must be connected to the fingerprint sensor's wake pin (default GPIO 3). If not connected, the task will fall back to 400ms polling - verify the fallback works too.