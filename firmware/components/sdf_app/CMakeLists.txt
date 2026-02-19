idf_component_register(
    SRCS "src/sdf_app.c"
    INCLUDE_DIRS "include"
    REQUIRES sdf_common sdf_services sdf_state_machines sdf_tasks sdf_protocol_ble sdf_protocol_zigbee sdf_drivers sdf_storage
)
