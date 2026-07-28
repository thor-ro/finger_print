set(cli_priv_reqs console esp_timer log nvs_flash sdf_services sdf_storage sdf_drivers sdf_protocol_zigbee sdf_platform sdf_ota)

if(NOT IDF_TARGET STREQUAL "linux")
    list(APPEND cli_priv_reqs sdf_app sdf_protocol_ble esp-zigbee-lib)
endif()

idf_component_register(
    SRCS "sdf_cli.c" "sdf_cli_commands.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES ${cli_priv_reqs}
)
