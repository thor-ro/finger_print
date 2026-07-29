## 1. NVS Storage Updates

- [ ] 1.1 Expand NVS schema to support web user account storage
- [ ] 1.2 Implement getters and setters for username and password hashes in `sdf_storage`

## 2. BLE Companion Service (GATT Server)

- [ ] 2.1 Add `ble-companion-service` initialization alongside existing BLE client
- [ ] 2.2 Implement BLE GATT Auth characteristic (read/write state machine)
- [ ] 2.3 Implement BLE GATT Config and OTA characteristics with Auth checks
- [ ] 2.4 Integrate Wi-Fi credential parsing from OTA characteristic and trigger Wi-Fi connection

## 3. Enrollment Task Updates

- [ ] 3.1 Update `sdf_enroll_task` to support the "Admin Auth" state for web registration
- [ ] 3.2 Add event router hooks for GATT server to request registration authorization
- [ ] 3.3 Link authorized registrations to `sdf_storage` to save web credentials

## 4. Web Companion App

- [ ] 4.1 Initialize GitHub Pages static web app repository structure
- [ ] 4.2 Implement Web Bluetooth connection and device discovery logic
- [ ] 4.3 Build registration and login UI flows (including local SHA256 hashing)
- [ ] 4.4 Build configuration and OTA triggering UI with battery warnings
