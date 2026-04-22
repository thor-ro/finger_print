# ADR 001: Selection of WS2812 LED Driver Peripheral on ESP32-C6
Date: 2026-04-22
Status: Accepted

## 1. Context and Problem Statement
The system uses an ESP32-C6 microcontroller to handle critical UART communication with external peripherals while simultaneously providing visual feedback via a WS2812 RGB LED (NeoPixel).Currently, standard software-based LED libraries (e.g., Adafruit_NeoPixel using bit-banging) are disabling CPU interrupts to meet the strict microsecond/nanosecond timing requirements of the WS2812 1-wire protocol. This interrupt blocking causes the ESP32-C6 to miss incoming UART bytes, resulting in corrupted or dropped data streams. We need a hardware-accelerated method to drive the WS2812 LED that does not block CPU interrupts.

## 2. Considered Options
To offload the precise timing requirements from the CPU, two hardware peripherals on the ESP32-C6 utilizing Direct Memory Access (DMA) were evaluated:
- Option 1: I2S (Inter-IC Sound) Peripheral
  - Mechanism: Repurposing the audio peripheral to generate custom waveforms. A single WS2812 bit is simulated by expanding it into multiple I2S bits (bit-faking) and transmitting them via DMA.
- Option 2: RMT (Remote Control) Peripheral
  - Mechanism: Using the dedicated pulse-generation peripheral. The RMT controller reads pulse duration specifications (high/low times) directly from RAM via DMA and generates the exact waveform required by the WS2812 protocol.

## 3. Decision
We will use the RMT (Remote Control) Peripheral (Option 2) to drive the WS2812 LED on the ESP32-C6.

## 4. Rationale
While both options successfully resolve the UART blocking issue by utilizing DMA and leaving CPU interrupts enabled, the RMT peripheral is the objectively better choice for the ESP32-C6 architecture for several reasons:
1. Purpose-Built Hardware: The RMT peripheral was explicitly designed by Espressif for generating and receiving arbitrary pulse waveforms (like IR remotes and LED strips). I2S is designed for continuous audio streams.
2. Memory Efficiency (RAM): The RMT approach is significantly more memory-efficient. It stores pulse duration instructions rather than a massively inflated bit-stream. The I2S workaround requires a large memory buffer to map every single WS2812 bit into an I2S word, which wastes valuable SRAM.
3. First-Party Support: Espressif provides an officially supported and highly optimized led_strip component in the ESP-IDF that leverages the RMT peripheral natively.
4. Hardware Compatibility: The I2S hardware architecture has changed in newer RISC-V chips like the ESP32-C6 compared to the legacy ESP32. Older I2S "hacks" for LEDs often break or require complex porting, whereas the RMT implementation is modernized and stable on the C6.

## 5. Consequences
- Positive: UART communication will remain stable and uninterrupted, with zero dropped bytes caused by LED updates.
  - Low SRAM footprint compared to I2S.
  - High maintainability due to reliance on official ESP-IDF APIs (esp-idf-led-strip or RMT-backed FastLED).
- Negative: The RMT peripheral has a limited number of TX channels (fewer on the C6 than on the original ESP32). However, since we only need to drive a single LED (or a single daisy-chained strip), utilizing one RMT channel is perfectly acceptable and will not cause resource starvation.
  - Requires refactoring the current LED driving code to replace the legacy bit-banging library.