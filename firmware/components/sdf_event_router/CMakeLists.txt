idf_component_register(
    SRCS "src/sdf_event_router.c"
    INCLUDE_DIRS "include"
    REQUIRES sdf_common sdf_storage
    PRIV_REQUIRES sdf_config
)