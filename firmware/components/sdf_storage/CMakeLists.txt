idf_component_register(
    SRCS "src/sdf_storage.c"
    INCLUDE_DIRS "include"
    REQUIRES sdf_common
    PRIV_REQUIRES nvs_flash esp_partition
)
