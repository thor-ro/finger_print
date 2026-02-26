set(app_srcs "src/sdf_app.c" "src/sdf_lock_flow.c")
set(app_reqs sdf_common sdf_services sdf_state_machines sdf_power sdf_protocol_ble sdf_protocol_zigbee sdf_drivers sdf_storage mbedtls)

if(IDF_TARGET STREQUAL "linux")
    # We still want to compile sdf_app on Linux for unit tests instead of excluding it.
endif()

idf_component_register(
    SRCS ${app_srcs}
    INCLUDE_DIRS "include"
    REQUIRES ${app_reqs}
)
