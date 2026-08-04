## Why

The `on_config_write` and `on_enroll_write` callbacks wired in `sdf_app_init()` only log the event and return. Any data written to the Config or Enroll GATT characteristics by the web companion is silently discarded. This means:
- Configuration changes via the companion app have no effect
- Fingerprint enrollment cannot be triggered over BLE despite the characteristic existing

This change implements the config read/write protocol and the BLE-triggered enrollment flow.

## What Changes

- `on_config_write`: parse the incoming payload as a JSON config delta, apply valid fields via `sdf_config_get_mutable()`, and send a confirmation notify back
- `on_enroll_write`: parse user_id + permission from the payload, call `sdf_services_request_enrollment()`, and track enrollment progress via events
- Add a `sdf_ble_companion_notify_config()` call to send current config snapshot when the Config characteristic is read
- Define a binary or JSON wire format for config and enroll commands

## Capabilities

### New Capabilities
- `ble-companion-config-rw`: Read and write device configuration over the BLE companion service
- `ble-companion-enroll`: Trigger fingerprint enrollment over the BLE companion service

### Modified Capabilities
- None

## Impact

- `firmware/components/sdf_app/src/sdf_app.c` — implement `sdf_ble_companion_on_config_write`, `sdf_ble_companion_on_enroll_write`
- `web-companion/app.js` — implement config read and enroll UI handlers
- `web-companion/index.html` — wire up the "Read Config" button and add enrollment UI
