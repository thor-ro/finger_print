## MODIFIED Requirements

### Requirement: Admin Action Handler
The `sdf_app` component SHALL handle the `SDF_SERVICES_ADMIN_ACTION_FACTORY_RESET` action by executing a complete factory reset sequence.

#### Scenario: Admin authorizes factory reset
- **WHEN** `sdf_app_on_admin_action()` receives `SDF_SERVICES_ADMIN_ACTION_FACTORY_RESET`
- **THEN** Call `sdf_storage_erase_all()` to wipe entire NVS
- **THEN** Call `fp_delete_all_users()` to clear fingerprint sensor
- **THEN** Call `sdf_protocol_zigbee_factory_reset()` to erase Zigbee NVRAM
- **THEN** Call `sdf_services_reset_state()` to reset internal state
- **THEN** Call `esp_restart()` to reboot
- **THEN** Device enters UNCLAIMED state on boot