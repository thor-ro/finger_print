set(service_reqs sdf_common sdf_storage sdf_drivers sdf_state_machines esp_timer)
if(NOT IDF_TARGET STREQUAL "linux")
    list(APPEND service_reqs led_strip)
endif()

idf_component_register(
    SRCS "src/sdf_services.c"
         "src/sdf_services_mock_linux.c"
    INCLUDE_DIRS "include"
    REQUIRES ${service_reqs}
)
