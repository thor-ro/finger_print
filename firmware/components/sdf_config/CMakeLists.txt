set(config_reqs sdf_platform sdf_protocol_zigbee)

idf_component_register(SRCS "src/sdf_config.c"
                       INCLUDE_DIRS "include"
                       REQUIRES ${config_reqs}
                       PRIV_REQUIRES nvs_flash)