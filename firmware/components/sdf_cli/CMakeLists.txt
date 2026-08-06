set(cli_priv_reqs console esp_timer log nvs_flash sdf_services sdf_storage sdf_drivers sdf_protocol_zigbee sdf_platform)

if(NOT IDF_TARGET STREQUAL "linux")
    # sdf_ota unconditionally requires app_update, which isn't built for
    # IDF_TARGET=linux; sdf_cli's own "ota" command is guarded out on that
    # target too (see sdf_cli_commands.c), so it doesn't need sdf_ota there.
    list(APPEND cli_priv_reqs sdf_ota sdf_app sdf_protocol_ble esp-zigbee-lib)
endif()

idf_component_register(
    SRCS "sdf_cli.c" "sdf_cli_commands.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES ${cli_priv_reqs}
)
