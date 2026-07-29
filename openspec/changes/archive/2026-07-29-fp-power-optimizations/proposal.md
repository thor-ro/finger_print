## Why

The fingerprint UART baud rate is currently 19200 (`CONFIG_SDF_FP_BAUD_RATE`), which is quite slow. The sensor supports 115200. By increasing the baud rate, we can significantly reduce the time spent waiting for `fp_match_1n()` UART transactions to complete. Less active CPU time equals a faster return to light/deep sleep, ultimately leading to longer battery life. Furthermore, the match task polls continuously at 400ms when the device is awake (`CONFIG_SDF_MATCH_POLL_INTERVAL_MS`), which is inefficient. Relying on the GPIO WAKE interrupt would be far more power efficient.

## What Changes

- Increase the fingerprint UART baud rate from 19200 to 115200.
- Make the match task polling interval adaptive or rely more heavily on the GPIO WAKE interrupt, reducing continuous 400ms polling when the device is awake.

## Capabilities

### New Capabilities
- `power-management`: Enhancements to peripheral power consumption and task scheduling for battery optimization.

### Modified Capabilities
- `sdf-services-tasks`: Updates to the match task to support interrupt-driven wake instead of continuous polling.

## Impact

- `sdf_drivers`: UART initialization baud rate change.
- `sdf_services` (`sdf_match_task`): Refactoring of the match task's polling loop to use GPIO interrupt wake-ups instead of a fixed 400ms poll.
- `sdkconfig.defaults`: Update default baud rate and polling configurations.
