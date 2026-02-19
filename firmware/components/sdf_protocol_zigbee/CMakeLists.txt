idf_component_register(
    SRCS "src/sdf_protocol_zigbee.c"
    INCLUDE_DIRS "include"
    REQUIRES sdf_common sdf_config sdf_platform
    PRIV_REQUIRES espressif__esp-zigbee-lib
)
