## Why

The web companion dashboard is missing its two core management features: the "Read Config" button does nothing, and there is no enrollment UI. These are the primary reasons the companion app exists — to allow fingerprint enrollment and device configuration from a smartphone without needing a Zigbee coordinator or serial CLI.

## What Changes

- Implement the "Read Config" button handler: fetch and display current device config
- Add a fingerprint enrollment panel: user_id input, permission dropdown, step-by-step guidance
- Add lock state and battery percent display on the dashboard (received via Config characteristic on connect)

## Capabilities

### New Capabilities
- None (web companion is a client; firmware side changes are in `fix-ble-config-enroll-callbacks`)

### Modified Capabilities
- None

## Impact

- `web-companion/app.js` — major expansion
- `web-companion/index.html` — add enrollment section and battery/lock state display
- `web-companion/style.css` — new styles for enrollment panel and status cards
- Depends on `fix-ble-config-enroll-callbacks` being applied first on the firmware side
