set(tasks_reqs sdf_common sdf_services sdf_state_machines sdf_protocol_ble sdf_protocol_zigbee sdf_drivers esp_timer)
if(NOT IDF_TARGET STREQUAL "linux")
    list(APPEND tasks_reqs driver esp_hw_support)
endif()

idf_component_register(
    SRCS "src/sdf_tasks.c"
    INCLUDE_DIRS "include"
    REQUIRES ${tasks_reqs}
)
