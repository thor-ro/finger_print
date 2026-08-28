idf_component_register(
    SRCS "lockout_reset_gate_main.c"
    INCLUDE_DIRS "."
    REQUIRES sdf_app sdf_config sdf_services sdf_storage sdf_event_router nvs_flash esp_timer
)
