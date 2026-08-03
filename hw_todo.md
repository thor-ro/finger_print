# Hardware Verification Todo

## fp-power-optimizations

These tasks require an ESP32-C6 device with a fingerprint sensor connected and a Nuki Smart Lock 3 Pro paired.

### 3.1 UART Baud Rate Stability Test
- [ ] Build and flash the firmware to a test device (`idf.py -p <PORT> flash`)
- [ ] Confirm 115200 baud UART communication is stable with the fingerprint sensor (no corruption, no timeouts)
- [ ] Verify `fp_probe()` and `fp_match_1n()` transactions complete significantly faster than at 19200 baud

### 3.2 Interrupt-Driven Match Task Verification
- [ ] Place a finger on the sensor while the device is awake and confirm the match task is awoken promptly
- [ ] Verify no continuous 400ms polling occurs when the device is idle (no finger present)
- [ ] Monitor CPU activity (via ESP-IDF profiler or GPIO toggling) to confirm the CPU returns to light/deep sleep between interrupts
- [ ] Confirm the WAKE GPIO ISR correctly notifies `sdf_match_task` via `xTaskNotifyFromISR`