# Setup and Flashing Guide

This document explains how to build, flash, and run the Smart Door Finger (SDF) firmware on your ESP32-C6.

## Prerequisites

1. **ESP-IDF v5.5.3**: 
   Ensure you have ESP-IDF v5.5.3 installed. If not, follow the official [ESP-IDF Get Started Guide](https://docs.espressif.com/projects/esp-idf/en/release-v5.5/esp32c6/get-started/index.html) to install it.
   
2. **Hardware**:
   - Waveshare ESP32-C6 mini module (or similar ESP32-C6 board)
   - USB cable for flashing
   - Fingerprint sensor and peripherals wired according to `doc/wiring.md`

## 1. Set up the Environment

Open your terminal and source the ESP-IDF environment variables:

```bash
# Sourcing the export script depends on where you installed ESP-IDF.
# Example:
. $HOME/esp/esp-idf/export.sh
```

Navigate to the `firmware` directory of the repository:

```bash
cd workspace/smart_door/firmware
```

## 2. Configuration (Optional)

You can customize the firmware configuration before building. The project contains different configuration profiles:
* `sdkconfig.defaults` (Base profile)
* `sdkconfig.debug.defaults` (Debug profile)
* `sdkconfig.release.defaults` (Release profile)

To modify settings (e.g., biometric thresholds, GPIO pins, etc.):

```bash
idf.py menuconfig
```

Select your desired profile by setting the `SDKCONFIG_DEFAULTS` variable. For example:

```bash
SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.release.defaults" idf.py reconfigure
```

## 3. Build the Firmware

Compile the application and all components by running:

```bash
idf.py build
```

*Note: The first build might take a few minutes as it compiles the toolchain and all dependencies.*

## 4. Flash the Firmware

Connect the ESP32-C6 to your computer. Find out which serial port it connects to (e.g., `/dev/ttyACM0`, `/dev/cu.usbmodem*` on Mac, or `COMx` on Windows). 

Flash the build onto the device:

```bash
idf.py -p /dev/cu.usbmodem1234 flash
```
*(Replace `/dev/cu.usbmodem1234` with your actual serial port. If you omit the `-p` parameter, ESP-IDF will attempt to auto-detect the port).*

## 5. Monitor Output (Make it run)

To observe logging output and verify that the firmware booted correctly, use the built-in monitor tool. This can be combined with the flash step:

```bash
idf.py -p /dev/cu.usbmodem1234 flash monitor
```

To exit the monitor at any time, press `Ctrl + ]`.

## Further Notes

- **NVS encryption is enabled**: Ensure you do not change the `partition_table.csv` which includes the `nvs_keys` partition, otherwise the encrypted NVS initialization will fail.
- **First Time Pairing Target**: Set the Nuki lock address correctly in `firmware/components/sdf_app/src/sdf_app.c` (`SDF_NUKI_TARGET_ADDR_TYPE` and `SDF_NUKI_TARGET_ADDR`) and recompile before attempting pairing.
