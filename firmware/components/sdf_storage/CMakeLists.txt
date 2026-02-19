idf_component_register(
    SRCS "src/sdf_storage.c"
    INCLUDE_DIRS "include"
    REQUIRES sdf_common sdf_config sdf_platform
    PRIV_REQUIRES nvs_flash
)
