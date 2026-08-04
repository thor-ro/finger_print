## Why

The SDF only updates Zigbee lock state when it initiates a BLE lock action. If the Nuki lock is operated manually from the inside, via the Nuki app, or via the Nuki keypad, the SDF has no awareness of the state change. The Zigbee coordinator's lock state attribute becomes stale, which may confuse automation rules that react to lock/unlock events.

## What Changes

- Add periodic polling of the Nuki keyturner state (e.g., every 30 seconds when not in sleep)
- Trigger a keyturner state poll on every wake from sleep (already partially done via `sdf_app_power_wakeup`)
- Subscribe to Nuki notifications/indications if the Nuki lock supports them (Nuki BLE API supports keyturner state notifications via the USDIO channel when the lock's security PIN changes or state changes)

## Capabilities

### New Capabilities
- `nuki-state-polling`: Periodic background polling of Nuki lock state to detect externally-initiated state changes

### Modified Capabilities
- None

## Impact

- `firmware/components/sdf_app/src/sdf_app.c` — add periodic poll timer or extend power wake path
- `firmware/components/sdf_power/src/sdf_power.c` — potentially integrate poll cadence with Zigbee check-in interval
- BLE radio will be activated more frequently if polling is enabled
