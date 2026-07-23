## ADDED Requirements

### Requirement: Fingerprint UART Driver
The `sdf_drivers` component SHALL provide a UART driver for the fingerprint sensor at 19200 baud with command framing, checksum validation, and response parsing.

#### Scenario: Command framing
- **WHEN** Sending command to sensor
- **THEN** Frame: [HEAD:2=0xEF01][ADDR:4][CMD:1][LEN:2][PAYLOAD:N][CHKSUM:2]
- **THEN** Checksum = sum of all bytes from CMD to end of PAYLOAD (mod 65536)

#### Scenario: Response parsing
- **WHEN** Receiving response from sensor
- **THEN** Parse HEAD, ADDR, CMD, LEN, PAYLOAD, CHKSUM
- **THEN** Validate checksum
- **THEN** Return parsed response struct with confirmation code

#### Scenario: Match 1:N command
- **WHEN** `fp_match_1n()` called
- **THEN** Send command 0x03 (AUTO_ENROLL? No, 0x03 is usually Match)
- **THEN** Wait for response with timeout `CONFIG_SDF_DRIVERS_FP_UART_TIMEOUT_MS` (default 12000ms)
- **THEN** On success: return user_id, permission, confidence
- **THEN** On timeout: return `FP_ERR_TIMEOUT`

#### Scenario: Enrollment step command
- **WHEN** `fp_enroll_step(step, user_id, permission)` called
- **THEN** Send command 0x04 (ENROLL_1) / 0x05 (ENROLL_2) / 0x06 (ENROLL_3)
- **THEN** Wait for ACK with timeout
- **THEN** Return `FP_ACK_OK` or `FP_ACK_FAIL`

#### Scenario: User management commands
- **WHEN** `fp_delete_user(user_id)` called → send 0x0C (DELETE)
- **WHEN** `fp_delete_all_users()` called → send 0x0D (EMPTY)
- **WHEN** `fp_get_user_count()` called → send 0x1D (GET_NUM)
- **WHEN** `fp_get_user_list()` called → send 0x1E (GET_LIST)

#### Scenario: LED control command
- **WHEN** `fp_control_led(mode, speed, color, count)` called
- **THEN** Send command 0x3C (CONTROL_LED) with payload bytes
- **THEN** NOTE: Payload byte values are module-variant specific; defaults in `sdf_services.c` may need tuning on real hardware

### Requirement: WS2812 LED Ring Driver
The `sdf_drivers` component SHALL drive a WS2812 LED ring via RMT peripheral for status feedback.

#### Scenario: LED patterns
- **WHEN** `led_set_color(r, g, b)` called → set all LEDs to RGB color
- **WHEN** `led_flash(color, duration_ms, count)` called → flash pattern
- **WHEN** `led_pulse(color, period_ms)` called → breathing pulse
- **WHEN** `led_breathe(color)` called → slow breathing animation
- **WHEN** `led_off()` called → all LEDs off

#### Scenario: Color mappings
- **WHEN** Green: match success, enrollment step success
- **WHEN** Red: error, fail, timeout
- **WHEN** Blue: awaiting admin auth, enrollment mode
- **WHEN** Yellow: pairing active
- **WHEN** Cyan/Purple: Zigbee join active
- **WHEN** White: unclaimed device

### Requirement: Battery ADC Driver
The `sdf_drivers` component SHALL read battery voltage via ADC with voltage divider and report percentage.

#### Scenario: Battery reading
- **WHEN** `battery_read_mv()` called
- **THEN** Sample ADC channel (GPIO 0) with attenuation 11dB
- **THEN** Apply voltage divider ratio (configured in Kconfig)
- **THEN** Return millivolts

#### Scenario: Battery percentage
- **WHEN** `battery_get_percentage()` called
- **THEN** Map mV to 0-100% using configured curve (LiPo 3.0V-4.2V default)
- **THEN** If < 20%: set low battery flag

### Requirement: GPIO Power Gating
The `sdf_drivers` component SHALL control fingerprint sensor power via GPIO EN pin.

#### Scenario: Sensor power on
- **WHEN** `fp_power_on()` called
- **THEN** Set GPIO EN high
- **THEN** Delay `CONFIG_SDF_DRIVERS_FP_POWER_ON_DELAY_MS` (default 50ms) for sensor boot

#### Scenario: Sensor power off
- **WHEN** `fp_power_off()` called
- **THEN** Set GPIO EN low

### Requirement: WAKE Pin Interrupt
The `sdf_drivers` component SHALL configure WAKE pin (GPIO 3) interrupt for fingerprint touch detection.

#### Scenario: Wake interrupt
- **WHEN** `fp_wake_enable(callback)` called
- **THEN** Configure GPIO 3 as input with pull-up, interrupt on falling edge
- **THEN** On interrupt: invoke callback, disable interrupt until re-enabled

### Requirement: Mock Interfaces for Testing
The `sdf_drivers` component SHALL provide mock implementations for Linux-based unit testing.

#### Scenario: Mock fingerprint driver
- **WHEN** `CONFIG_SDF_DRIVERS_MOCK=1`
- **THEN** `fp_match_1n()` returns configurable test data
- **THEN** `fp_enroll_step()` simulates ACK sequence
- **THEN** No hardware UART required

#### Scenario: Mock LED driver
- **WHEN** Mock enabled
- **THEN** `led_*()` functions log calls instead of driving RMT

#### Scenario: Mock battery driver
- **WHEN** Mock enabled
- **THEN** `battery_read_mv()` returns configurable test voltage