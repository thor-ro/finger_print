## Why

The BLE companion service advertises continuously at fast interval (30–60ms). After a short initial discoverability window with no connection, slow advertising (1000–2500ms) would reduce radio-on time by ~16×. This is a meaningful power saving for a battery-powered device.

## What Changes

- After 30 seconds of advertising with no connection, switch from fast to slow advertising interval
- On disconnect, restart at fast interval for 30 seconds then revert to slow
- Advertise at slow interval indefinitely when at least one connection slot is free

## Capabilities

### New Capabilities
- None

### Modified Capabilities
- None

## Impact

- `firmware/components/sdf_ble_companion/src/sdf_ble_companion.c` — advertising logic
- Battery life improvement when device is not actively being configured
