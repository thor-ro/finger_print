## Architecture

The fingerprint sensor will remain the source of truth for biometric templates and existence of a user. The ESP32-C6 NVS acts as the source of truth for the human-readable names.

We enforce a strict consistency model:
- When a user is deleted from the sensor, their name is deleted from NVS.
- When the sensor is factory reset, NVS is wiped.
- When querying users, the system joins the sensor's authoritative list of IDs with the names from NVS.

### Data Model
- **Capacity**: Maximum 10 users.
- **Name Length**: Maximum 32 characters (null-terminated).
- **Storage Strategy**: Store as individual NVS keys (e.g., `user_name_01` to `user_name_10`) within the `sdf` namespace. This keeps read/writes simple and avoids parsing a unified JSON blob for internal operations.

### Interfaces

#### CLI (`sdf_cli`)
- Add command: `user set-name <id> <name>`
- Modify `user list` to print the name column.

#### BLE Companion (`ble-companion-service`)
- Extend the `Config` characteristic or add a new `User Management` characteristic that accepts read/write of a JSON array: `[{"id": 1, "name": "Alice", "perm": 3}]`.

#### Zigbee (`sdf_protocol_zigbee`)
- The existing custom `0x4000` Active Users attribute payload changes from `[1:3, 5:1]` to `[{"id":1,"perm":3,"name":"Alice"}]`.
