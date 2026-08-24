idf_component_register(
    SRCS "ble_ota_gate_main.c"
    INCLUDE_DIRS "."
    REQUIRES sdf_app sdf_ble_companion sdf_services
)
