set(cli_priv_reqs console esp_timer log nvs_flash sdf_services sdf_storage sdf_drivers sdf_protocol_zigbee sdf_platform sdf_device_state)

if(NOT IDF_TARGET STREQUAL "linux")
    # sdf_ota unconditionally requires app_update, which isn't built for
    # IDF_TARGET=linux; sdf_cli's own "ota" command is guarded out on that
    # target too (see sdf_cli_commands.c), so it doesn't need sdf_ota there.
    list(APPEND cli_priv_reqs sdf_ota sdf_app sdf_protocol_ble esp-zigbee-lib)
endif()

# sdf_cli.c is always built - it provides sdf_cli_init() and friends
# unconditionally so callers (sdf_app) never need to know whether the debug
# console is enabled. When CONFIG_SDF_CLI_ENABLE is unset, sdf_cli.c falls
# back to a no-op stub implementation and sdf_cli_commands.c (the actual
# esp_console/argtable3/linenoise-backed command set) is excluded from the
# build entirely, so the linker drops those dependencies from the image.
# (REQUIRES/PRIV_REQUIRES can't be made conditional on CONFIG_ values - they
# are expanded before Kconfig is loaded - so gating happens via SRCS/#ifdef
# instead. See ESP-IDF docs, api-guides/build-system.rst.)
set(cli_srcs "sdf_cli.c")
if(CONFIG_SDF_CLI_ENABLE)
    list(APPEND cli_srcs "sdf_cli_commands.c")
endif()

idf_component_register(
    SRCS ${cli_srcs}
    INCLUDE_DIRS "include"
    PRIV_REQUIRES ${cli_priv_reqs}
)
