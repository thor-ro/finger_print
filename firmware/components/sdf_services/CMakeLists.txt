set(service_reqs sdf_common sdf_storage sdf_drivers sdf_state_machines esp_timer sdf_platform sdf_config sdf_event_router sdf_app)
if(NOT IDF_TARGET STREQUAL "linux")
    list(APPEND service_reqs button)
endif()

idf_component_register(
    SRCS "src/sdf_services.c" "src/sdf_services_match.c" "src/sdf_services_enroll.c" "src/sdf_services_admin.c" "src/sdf_services_button.c"
    INCLUDE_DIRS "include"
    REQUIRES ${service_reqs}
)
