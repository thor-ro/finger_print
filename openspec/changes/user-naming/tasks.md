## Execution Plan

### 1. Storage Layer
- [ ] Add `sdf_storage_save_user_name(uint16_t user_id, const char *name)` to `sdf_storage`.
- [ ] Add `sdf_storage_load_user_name(uint16_t user_id, char *name_out, size_t max_len)` to `sdf_storage`.
- [ ] Add `sdf_storage_delete_user_name(uint16_t user_id)` to `sdf_storage`.
- [ ] Add unit tests for user name storage.

### 2. Services Layer
- [ ] Update `sdf_services_delete_user` to call `sdf_storage_delete_user_name`.
- [ ] Update `sdf_services_clear_all_users` to iterate and clear all names (or wipe NVS namespace).

### 3. CLI Integration
- [ ] Add `cmd_user_set_name` handler to `sdf_cli_commands.c`.
- [ ] Update `cmd_user_list` to fetch and display the name alongside the ID.
- [ ] Update `cmd_user_get` to display the name.

### 4. Zigbee Sync
- [ ] Update `sdf_app_update_zigbee_user_list()` in `sdf_app.c` to fetch names and serialize them as JSON (e.g. `[{"id":1,"perm":3,"name":"Alice"}]`) before updating the Zigbee attribute.

### 5. BLE Companion Sync
- [ ] Ensure the BLE Config characteristic (to be implemented in `ble-companion-app`) uses the same getter/setter logic for user names.
