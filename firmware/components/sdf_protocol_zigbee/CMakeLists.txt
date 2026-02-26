set(zg_priv_reqs "")
if(NOT IDF_TARGET STREQUAL "linux")
    list(APPEND zg_priv_reqs espressif__esp-zigbee-lib)
endif()

idf_component_register(
    SRCS "src/sdf_protocol_zigbee.c"
         "src/sdf_protocol_zigbee_mock_linux.c"
    INCLUDE_DIRS "include"
    REQUIRES sdf_common
    PRIV_REQUIRES ${zg_priv_reqs}
)
