idf_component_register(
    SRCS "src/sdf_device_state.c"
    INCLUDE_DIRS "include"
    REQUIRES sdf_ota sdf_protocol_zigbee sdf_services
)
