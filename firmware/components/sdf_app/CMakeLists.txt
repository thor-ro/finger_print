set(app_srcs "src/sdf_app.c" "src/sdf_lock_flow.c")
set(app_reqs sdf_common sdf_services sdf_state_machines sdf_power sdf_protocol_ble sdf_protocol_zigbee sdf_drivers sdf_storage)

if(IDF_TARGET STREQUAL "linux")
    set(app_srcs "")
    set(app_reqs "")
endif()

idf_component_register(
    SRCS ${app_srcs}
    INCLUDE_DIRS "include"
    REQUIRES ${app_reqs}
)
