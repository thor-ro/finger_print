idf_component_register(
    SRCS "src/sdf_services.c"
    INCLUDE_DIRS "include"
    REQUIRES sdf_common sdf_config sdf_platform sdf_storage
)
