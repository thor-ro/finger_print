idf_component_register(
    SRCS "sdf_cli.c" "sdf_cli_commands.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES console esp_timer log nvs_flash sdf_services
)
