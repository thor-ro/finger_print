# Hardware Wiring

The following table describes the wiring of the Waveshare ESP32-C6 mini module to the fingerprint sensor and other peripherals.

| Breakout Pin | ESP32-C6 Pin | Peripheral Pin   | Description                                     |
| ------------ | ------------ | ---------------- | ----------------------------------------------- |
| 5V           | 5V           |                  | 5V Power Input                                  |
| GND          | GND          | Fingerprint GND  | Ground                                          |
| 3.3V         | 3.3V         | Fingerprint 3.3V | 3.3V Power (Output)                             |
| 0            | GPIO0        | Fingerprint RX   | UART TX (Data to sensor)                        |
| 1            | GPIO1        | Fingerprint TX   | UART RX (Data from sensor)                      |
| 2            | GPIO2        | Fingerprint EN   | Power Enable / High = Active, Low = Sleep       |
| 3            | GPIO3        | Fingerprint Wake | Capacitive touch wake signal                    |
| 4            | GPIO4        | Push Button      | Local Enrollment Button (Active Low)            |
| 5            | GPIO5        | Battery ADC      | Voltage divider (1MΩ / 1MΩ) for battery reading |
| 6            | GPIO6        |                  |                                                 |
| 7            | GPIO7        |                  |                                                 |
| 8            | GPIO8        |                  | WS2812 RGB LED (onboard)                        |
| 9            | GPIO9        |                  |                                                 |
| 12           | GPIO12       |                  |                                                 |
| 13           | GPIO13       |                  |                                                 |
| 14           | GPIO14       |                  |                                                 |
| 15           | GPIO15       |                  |                                                 |
| 18           | GPIO18       |                  |                                                 |
| 19           | GPIO19       |                  |                                                 |
| 20           | GPIO20       |                  |                                                 |
| 21           | GPIO21       |                  |                                                 |
| 22           | GPIO22       |                  |                                                 |
| 23           | GPIO23       |                  |                                                 |
| RX           | UART0 RX     |                  | Console / Debug RX                              |
| TX           | UART0 TX     |                  | Console / Debug TX                              |

## Power Supply (Battery Operation)

When powering the system using two 1.5V AA batteries (3V total) and the Pololu 5V Step-Up Voltage Regulator (U1V11F5 / 2562), it is crucial to add decoupling and buffer capacitors to prevent voltage spikes and brownout resets:

1. **Input Stage (Before the Step-Up Regulator):**
   - **Capacitor:** 33 µF (or larger) Electrolytic Capacitor
   - **Placement:** Between `VIN` and `GND` of the Pololu regulator, placed as close to the regulator pins as possible.
   - **Purpose:** Protects the regulator from destructive LC voltage spikes that occur when the batteries are connected.

2. **Output Stage (Between Regulator and ESP32-C6):**
   - **Capacitor:** 100 µF to 470 µF Electrolytic Capacitor (Low ESR preferred)
   - **Placement:** Between `5V Output` and `GND` of the Pololu regulator (or directly at the 5V input of the ESP32 board).
   - **Purpose:** The ESP32 draws high peak currents (up to ~500mA) during Wi-Fi or BLE transmissions. Since AA batteries have high internal resistance and step-up regulators have a transient response delay, this bulk capacitor acts as an energy buffer. It prevents the 5V line from sagging and causing an ESP32 brownout reset.
   - **Optional/Additional:** A 10 µF ceramic capacitor and a 0.1 µF ceramic capacitor in parallel close to the ESP32's power input can further help filter out high-frequency noise.

## Push Button Wiring

The push button (used for local enrollment, network joining, etc.) is an Active Low button connected directly between **GPIO 14** and **GND**.

*   **Internal Pull-up:** The firmware configures GPIO 14 with the ESP32's internal pull-up resistor enabled. This keeps the pin securely HIGH (3.3V) when the button is not pressed.
*   **External Pull-up (Optional):** If you are using long wires to connect the button (e.g., routing it to the outside of an enclosure), the wire can act as an antenna and pick up electromagnetic noise. In such cases, adding an external **10kΩ pull-up resistor** between GPIO 14 and the 3.3V pin is recommended to prevent false button presses.

