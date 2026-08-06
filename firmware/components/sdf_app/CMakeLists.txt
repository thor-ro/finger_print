set(app_srcs "src/sdf_app.c" "src/sdf_lock_flow.c")
set(app_reqs sdf_common sdf_services sdf_state_machines sdf_power sdf_protocol_ble sdf_protocol_zigbee sdf_drivers sdf_storage mbedtls sdf_platform sdf_config sdf_event_router sdf_ble_companion sdf_cli)

if(IDF_TARGET STREQUAL "linux")
    # We still want to compile sdf_app on Linux for unit tests instead of excluding it.
endif()

# NOTE: sdf_cli is required unconditionally here - REQUIRES/PRIV_REQUIRES are
# expanded before Kconfig is loaded, so they cannot depend on CONFIG_ values
# (see ESP-IDF docs, api-guides/build-system.rst). The CONFIG_SDF_CLI_ENABLE
# gate instead lives inside sdf_cli itself (its CMakeLists.txt conditionally
# compiles the console-backed sources, and sdf_cli.c falls back to a no-op
# stub), so when disabled, nothing actually references esp_console/argtable3/
# linenoise and the linker drops them from the final image.
idf_component_register(
    SRCS ${app_srcs}
    INCLUDE_DIRS "include"
    REQUIRES ${app_reqs}
)
