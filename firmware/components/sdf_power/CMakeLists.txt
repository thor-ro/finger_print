set(tasks_reqs sdf_common sdf_services sdf_state_machines sdf_protocol_ble sdf_drivers esp_timer sdf_platform sdf_config sdf_event_router)
if(NOT IDF_TARGET STREQUAL "linux")
    list(APPEND tasks_reqs sdf_protocol_zigbee driver esp_hw_support)
endif()

idf_component_register(
    SRCS "src/sdf_power.c"
    INCLUDE_DIRS "include"
    REQUIRES ${tasks_reqs}
)
