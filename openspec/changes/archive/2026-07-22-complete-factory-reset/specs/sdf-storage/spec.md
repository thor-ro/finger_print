## MODIFIED Requirements

### Requirement: Full NVS Erase
The `sdf_storage` component SHALL provide a function to completely erase the NVS namespace and reinitialize NVS.

#### Scenario: Erase all NVS data
- **WHEN** `sdf_storage_erase_all()` is called
- **THEN** Call `nvs_flash_erase()` to wipe entire NVS partition
- **THEN** Call `nvs_flash_init()` to reinitialize NVS
- **THEN** Return `ESP_OK` on success
- **THEN** Subsequent `sdf_storage_nuki_load()` returns `ESP_ERR_NOT_FOUND`

### Requirement: BLE Target Address Clear (Selective Fallback)
The `sdf_storage` component SHALL provide a function to selectively clear the BLE target address key.

#### Scenario: Clear BLE target address
- **WHEN** `sdf_storage_ble_target_clear()` is called
- **THEN** Erase key "ble_target" from NVS namespace
- **THEN** Return `ESP_OK` on success