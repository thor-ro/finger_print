## ADDED Requirements

### Requirement: GPIO Abstraction
The `sdf_platform` component SHALL provide GPIO wrapper functions for input/output, interrupt configuration, and power gating.

#### Scenario: GPIO init and control
- **WHEN** `sdf_platform_gpio_init(pin, mode, pull)` called
- **THEN** Configure ESP32-C6 GPIO with esp-idf `gpio_config_t`
- **WHEN** `sdf_platform_gpio_set_level(pin, level)` called
- **THEN** Set output level
- **WHEN** `sdf_platform_gpio_get_level(pin)` called
- **THEN** Read input level
- **WHEN** `sdf_platform_gpio_install_isr(pin, handler, arg)` called
- **THEN** Install ISR with `gpio_isr_handler_add()`

### Requirement: UART Abstraction
The `sdf_platform` component SHALL provide UART wrapper for fingerprint sensor communication (UART1, 19200 baud).

#### Scenario: UART init
- **WHEN** `sdf_platform_uart_init(uart_num, baud, tx_pin, rx_pin)` called
- **THEN** Configure UART with 8N1, 19200 baud, no flow control
- **THEN** Install driver with 256-byte RX/TX buffers

#### Scenario: UART read/write
- **WHEN** `sdf_platform_uart_write(data, len, timeout)` called
- **THEN** Write bytes with timeout
- **WHEN** `sdf_platform_uart_read(buf, len, timeout)` called
- **THEN** Read bytes with timeout, return bytes read

### Requirement: ADC Abstraction
The `sdf_platform` component SHALL provide ADC wrapper for battery voltage reading (GPIO 0, voltage divider).

#### Scenario: ADC read
- **WHEN** `sdf_platform_adc_init(channel, atten)` called
- **THEN** Configure ADC1 channel with attenuation (default 11dB for 3.3V)
- **WHEN** `sdf_platform_adc_read_raw(channel, &raw)` called
- **THEN** Read raw value, apply calibration
- **WHEN** `sdf_platform_adc_read_voltage(channel, &mv)` called
- **THEN** Return calibrated millivolts

### Requirement: RMT/LED Strip Abstraction
The `sdf_platform` component SHALL provide RMT wrapper for WS2812 LED ring control (GPIO 8).

#### Scenario: LED strip init
- **WHEN** `sdf_platform_led_strip_init(gpio, led_count)` called
- **THEN** Configure RMT channel with WS2812 timing (800kHz)
- **WHEN** `sdf_platform_led_strip_set_pixel(index, r, g, b)` called
- **THEN** Set pixel color in buffer
- **WHEN** `sdf_platform_led_strip_refresh()` called
- **THEN** Transmit buffer via RMT

### Requirement: FreeRTOS Primitives
The `sdf_platform` component SHALL provide task, queue, timer, and semaphore wrappers.

#### Scenario: Task creation
- **WHEN** `sdf_platform_task_create(name, func, stack, prio, handle)` called
- **THEN** Call `xTaskCreatePinnedToCore()` (core 0 for single-core C6)

#### Scenario: Queue operations
- **WHEN** `sdf_platform_queue_create(len, item_size)` called
- **THEN** Create FreeRTOS queue
- **WHEN** `sdf_platform_queue_send/receive()` called
- **THEN** Wrap `xQueueSend/Receive()` with timeout

### Requirement: NVS Abstraction
The `sdf_platform` component SHALL provide NVS wrapper for key-value storage.

#### Scenario: NVS open/close
- **WHEN** `sdf_platform_nvs_open(namespace, mode, handle)` called
- **THEN** Call `nvs_open()` with namespace "sdf"
- **WHEN** `sdf_platform_nvs_close(handle)` called
- **THEN** Call `nvs_close()`

### Requirement: Time/Sleep Abstraction
The `sdf_platform` component SHALL provide time and deep sleep wrappers.

#### Scenario: Time functions
- **WHEN** `sdf_platform_get_time_us()` called
- **THEN** Return `esp_timer_get_time()`
- **WHEN** `sdf_platform_deep_sleep_start()` called
- **THEN** Call `esp_deep_sleep_start()`

### Requirement: Logging Abstraction
The `sdf_platform` component SHALL provide log macros mapping to ESP_LOG.

#### Scenario: Log macros
- **WHEN** `SDF_LOGI(tag, fmt, ...)` called
- **THEN** Expand to `ESP_LOGI(tag, fmt, ...)`
- **WHEN** `SDF_LOGW/LOGE/LOGD` similarly