# Hardware Wiring

The following table describes the wiring of the ESP32-C6 to the fingerprint sensor and other peripherals.

| Breakout Pin| ESP32-C6 Pin | Peripheral Pin | Description |
|-------------|--------------|----------------|-------------|
| TX | TX | Fingerprint RX | |
| RX | RX | Fingerprint TX | |
| 0 | GPIO0, ADC A0 | | |
| 1 | GPIO1, ADC A1 | | |
| 2 | GPIO2, ADC A2 | | |
| 3 | GPIO3, ADC A3 | | |
| 4 | GPIO4, ADC A4 | | |
| 5 | GPIO5, ADC A5 | | |
| 6 | GPIO6, ADC A6 | Fingerprint Wake | |
| 7 | GPIO7 | Fingerprint RST/Power Enable | High = Active, Low = Sleep |
| 8 | GPIO8 | | |
| 9 | GPIO9 | | |
| 10 | GPIO10 | | |
| 11 | GPIO11 | | |
| 12 | GPIO12 | | |
| 13 | GPIO13 | | |
| 14 | GPIO14 | | |
| 15 | GPIO15 | | |
| 16 | GPIO16 | | |
| 17 | GPIO17 | | |
| 18 | GPIO18 | | |
| 20 | GPIO20 | | |
| 21 | GPIO21 | | |
| 22 | GPIO22 | | |
| 23 | GPIO23 | | |
| BAT | | | |
| 3,3V | 3,3V | Fingerprint 3,3V | |
| GND | GND | Fingerprint GND | |
| 5V | 5V | | |
| LED | GPIO15 | LED Yellow | onboard led |

