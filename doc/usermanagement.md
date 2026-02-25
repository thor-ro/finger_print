# Lightweight User Management Concept

The fingerprint sensor on the smart door lock supports an internal hardware database capable of storing up to 500 fingerprints with assigned User IDs (12-bit) and permissions (1, 2, 3). To integrate this seamlessly with a Zigbee Smart Home Coordinator (e.g., Home Assistant, Zigbee2MQTT), we map the fingerprint hardware features to the standard **Zigbee Cluster Library (ZCL) Door Lock Cluster (0x0101)**.

## 1. Adding Users (Enrollment)

We overload the standard Zigbee PIN/RFID credential setup commands to trigger a local biometric enrollment state machine. 

### Zigbee Triggers
- `Set PIN Code` (Command ID: `0x05`)
- `Set RFID Code` (Command ID: `0x16`)

**Workflow:**
1. The Smart Home network sends a `Set PIN Code` command with a specified `User ID` (e.g., `0x0001`) and a `User Type` / `User Status`.
2. The `sdf_app` layer traps this command and initiates `sdf_services_request_enrollment(user_id, permission)` instead of generating an actual PIN.
3. The Lock enters an active enrollment phase (flashing LEDs, powering up the sensor).
4. The user places their finger on the sensor three times (standard fingerprint template generation: commands `0x01`, `0x02`, `0x03`).
5. Upon successful biometric capture, the lock saves the biometric template under the provided `User ID` inside the DSP module.

## 1.1 Local Hardware Enrollment

To allow for offline and physical onboarding, the door lock provides a local enrollment flow utilizing a physical GPIO button mapped to the ESP32.

**Workflow:**
1. **Initiation:** The configuration button is held down for a long press (e.g., 3-5 seconds).
2. **Setup State:** The connected status LED begins to blink slowly, indicating the lock is awaiting an administrator.
3. **Admin Verification:** An already-enrolled user (with an appropriate "Admin" permission level) must successfully scan their finger on the sensor to unlock the secure enrollment configuration.
4. **Active Enrollment:** Once the admin identity is verified, the LED transitions to an active enrollment state (e.g., rapid flashing or solid alternating colors).
5. **Execution:** The standard Zigbee-style enrollment phase continues seamlessly (`sdf_services_request_enrollment`). The local system automatically assigns the next available sequential `User ID` internally, and the new user scans their finger three times to build the template profile.

## 2. Removing Users

Similarly, the standard Zigbee deletion commands trigger the fingerprint sensor's internal deletion functions.

### Zigbee Triggers
- `Clear PIN Code` (Command ID: `0x06`) / `Clear RFID Code` (Command ID: `0x17`)
  - **Action:** Sends UART command `0x04` to the fingerprint sensor with the specified `User ID`.
- `Clear All PIN Codes` (Command ID: `0x07`) / `Clear All RFID Codes` (Command ID: `0x18`)
  - **Action:** Sends UART command `0x05` to the fingerprint sensor, formatting the entire user database.

## 3. Exposing the User List to the Smart Home

To make the internal list of active fingerprints available to the Zigbee network, the firmware utilizes the fingerprint sensor's **Query information of all users** command (`0x2B`).

### Proposed Implementation: Custom ZCL Attribute
Due to Zigbee's limited capability to fetch bulk user lists natively through the Door Lock cluster, we expose the list via a **Manufacturer-Specific ZCL Attribute**.

1. **Custom Attribute Definition:** 
   - We define a new attribute `ESP_ZB_ZCL_ATTR_DOOR_LOCK_ACTIVE_USERS_LIST` (e.g., `0x4000`) inside the Door Lock Cluster, marked with a custom manufacturer code.
   - The attribute type is a **Byte Array** or **Character String**.
2. **Data Generation:**
   - Every time a user is added, deleted, or the lock reboots, the firmware queries the fingerprint module (`0x2B`) to obtain the current ID table (3 bytes per user: High ID, Low ID, Permission).
   - This table is packed into a JSON array or a structured binary string (e.g., `[1,4,15]`) and written to the custom attribute.
3. **Coordinator Sync:**
   - The Zigbee Coordinator binds to this attribute and is notified natively upon any changes.
   - Platforms like Home Assistant can instantly decode this string and render a dashboard showing "Active User IDs: 1, 4, 15", allowing the administrator to click and revoke specific IDs using the "Clear PIN Code" command.
