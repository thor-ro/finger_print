set(service_reqs sdf_common sdf_storage sdf_drivers sdf_state_machines sdf_protocol_ble esp_timer sdf_platform sdf_config sdf_event_router esp_hal_gpio)
if(NOT IDF_TARGET STREQUAL "linux")
    list(APPEND service_reqs button sdf_app)
endif()

idf_component_register(
    SRCS "src/sdf_services.c" "src/sdf_services_match.c" "src/sdf_services_enroll.c" "src/sdf_services_admin.c" "src/sdf_services_button.c" "src/sdf_services_web_auth.c" "src/sdf_services_setup.c" "src/sdf_services_mock_linux.c"
    INCLUDE_DIRS "include" "../sdf_app/include"
    REQUIRES ${service_reqs}
    PRIV_REQUIRES mbedtls
)
