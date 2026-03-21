# Hardware Wiring

The following table describes the wiring of the Waveshare ESP32-C6 mini module to the fingerprint sensor and other peripherals.

| Breakout Pin | ESP32-C6 Pin | Peripheral Pin | Description |
| ------------ | ------------ |----------------|-------------|
| 5V           | 5V           |                | 5V Power Input |
| GND          | GND          | Fingerprint GND| Ground |
| 3.3V         | 3.3V         | Fingerprint 3.3V| 3.3V Power (Output) |
| 0            | GPIO0        | Battery ADC    | Voltage divider (1MΩ / 1MΩ) for battery reading |
| 1            | GPIO1        |                | |
| 2            | GPIO2        | Fingerprint EN | Power Enable / High = Active, Low = Sleep |
| 3            | GPIO3        | Fingerprint Wake| Capacitive touch wake signal |
| 4            | GPIO4        | Fingerprint RX | UART TX (Data to sensor) |
| 5            | GPIO5        | Fingerprint TX | UART RX (Data from sensor) |
| 6            | GPIO6        |                | |
| 7            | GPIO7        |                | |
| 8            | GPIO8        |                | WS2812 RGB LED (onboard) |
| 9            | GPIO9        |                | |
| 12           | GPIO12       |                | |
| 13           | GPIO13       |                | |
| 14           | GPIO14       | Push Button    | Local Enrollment Button (Active Low) |
| 15           | GPIO15       |                | |
| 18           | GPIO18       |                | |
| 19           | GPIO19       |                | |
| 20           | GPIO20       |                | |
| 21           | GPIO21       |                | |
| 22           | GPIO22       |                | |
| 23           | GPIO23       |                | |
| RX           | UART0 RX     |                | Console / Debug RX |
| TX           | UART0 TX     |                | Console / Debug TX |
