## 1. NVS Storage Updates

- [x] 1.1 Expand NVS schema to support web user account storage
- [x] 1.2 Implement getters and setters for username and password hashes in `sdf_storage`

## 2. BLE Companion Service (GATT Server)

- [x] 2.1 Add a pre-start Companion GATT registration hook to the existing NimBLE host; make `sdf_protocol_ble` the sole NimBLE lifecycle owner
- [x] 2.2 Implement BLE GATT Auth characteristic validation against `sdf_storage`, including per-connection authenticated state and result notification
- [x] 2.3 Implement BLE GATT Config, Enrollment, and OTA characteristics with per-connection Auth checks
- [x] 2.4 Parse bounded OTA JSON (`ssid`, `password`, `firmwareUrl`), connect Wi-Fi, and stream HTTPS downloads through the existing verified OTA flow

## 3. Enrollment Task Updates

- [x] 3.1 Update `sdf_admin_task` to support the "Admin Auth" state for web registration
- [x] 3.2 Add event router hooks for the originating GATT connection to request and receive registration authorization
- [x] 3.3 Link authorized registrations to `sdf_storage`, then authenticate and notify the originating GATT connection

## 4. Web Companion App

- [x] 4.1 Initialize the `web-companion/` GitHub Pages static app structure and deployment documentation
- [x] 4.2 Implement Web Bluetooth connection and device discovery logic
- [x] 4.3 Build registration and login UI flows (including local SHA256 hashing)
- [x] 4.4 Build configuration and OTA triggering UI with battery warnings and HTTPS firmware URL selection
