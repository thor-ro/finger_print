## MODIFIED Requirements

### Requirement: Zigbee Factory Reset
The `sdf_protocol_zigbee` component SHALL provide a function to erase Zigbee NVRAM and leave the network.

#### Scenario: Factory reset Zigbee stack
- **WHEN** `sdf_protocol_zigbee_factory_reset()` is called
- **THEN** Call `esp_zb_bdb_factory_reset()` to erase Zigbee NVRAM
- **THEN** If joined to network, call `esp_zb_bdb_leave_network()` to leave
- **THEN** Call `esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_INITIALIZATION)` to reinitialize
- **THEN** Return `ESP_OK` on success
- **THEN** Subsequent `esp_zb_bdb_is_factory_new()` returns true