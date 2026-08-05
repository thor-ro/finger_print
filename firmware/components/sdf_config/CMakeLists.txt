set(config_reqs sdf_platform)
if(NOT IDF_TARGET STREQUAL "linux")
    list(APPEND config_reqs sdf_protocol_zigbee)
endif()

idf_component_register(SRCS "src/sdf_config.c"
                       INCLUDE_DIRS "include"
                       REQUIRES ${config_reqs}
                       PRIV_REQUIRES nvs_flash)