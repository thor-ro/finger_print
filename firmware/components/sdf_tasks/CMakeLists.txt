idf_component_register(
    SRCS "src/sdf_tasks.c"
    INCLUDE_DIRS "include"
    REQUIRES sdf_common sdf_services sdf_state_machines sdf_protocol_ble sdf_protocol_zigbee sdf_drivers driver esp_timer esp_hw_support
)
