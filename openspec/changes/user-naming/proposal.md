## Why
Displaying raw user IDs (1-10) is hostile for users. Adding a human-readable text string to be associated with an ID makes Zigbee management and local CLI management much more user-friendly. Since the system supports up to 10 users max, storing short names locally has a negligible memory footprint.

## What Changes
- Add persistent storage for User Names in the ESP32-C6 NVS (`sdf_storage`).
- Update `sdf_cli` to support setting and displaying User Names.
- Expose User Naming over the `ble-companion-app` (GATT).
- Sync names to the Zigbee Coordinator using the custom Active Users attribute (`0x4000`).

## Capabilities
### New Capabilities
- `sdf-user-naming`: Handles mapping of user ID (1-10) to Name.

### Modified Capabilities
- `sdf-cli`: Modified to support `user set-name <id> <name>` and displaying names in `user list`.
- `ble-companion-service`: Expose a GATT characteristic to manage names.
- `sdf-protocol-zigbee`: Sync active user list payload with embedded names.

## Impact
- `sdf_storage`: NVS schema expands to store up to 10 user names (max 32 bytes each).
- Deletion logic inside `sdf_services` will also invoke `sdf_storage_delete_user_name()` to keep names strictly in sync with biometric templates.
