set(zg_priv_reqs "")
if(NOT IDF_TARGET STREQUAL "linux")
    list(APPEND zg_priv_reqs espressif__esp-zigbee-lib)
endif()

idf_component_register(
    SRCS "src/sdf_protocol_zigbee.c"
    INCLUDE_DIRS "include"
    REQUIRES sdf_common sdf_config sdf_platform
    PRIV_REQUIRES ${zg_priv_reqs}
)
