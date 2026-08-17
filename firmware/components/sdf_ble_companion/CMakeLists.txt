idf_component_register(SRCS "src/sdf_ble_companion.c" "src/sdf_ble_companion_ota.c" "src/sdf_ble_ota_protocol.c" "src/sdf_ble_companion_bond_state.c" "src/sdf_ble_companion_gatt_scratch.c"
                    INCLUDE_DIRS "include"
                    REQUIRES bt espressif__cjson sdf_ota sdf_protocol_ble sdf_storage sdf_event_router sdf_services
                    PRIV_REQUIRES freertos)
