set(zg_priv_reqs "")
if(NOT IDF_TARGET STREQUAL "linux")
    list(APPEND zg_priv_reqs espressif__esp-zigbee-lib app_update esp_timer spi_flash sdf_ota)
endif()

idf_component_register(
    # sdf_zigbee_attr_cache.c builds on both targets on purpose: it carries the
    # cache-mutex/stack-lock ordering, which host tests must be able to drive.
    SRCS "src/sdf_protocol_zigbee.c"
         "src/sdf_protocol_zigbee_mock_linux.c"
         "src/sdf_zigbee_attr_cache.c"
    INCLUDE_DIRS "include"
    REQUIRES sdf_common
    PRIV_REQUIRES ${zg_priv_reqs} sdf_config
)
