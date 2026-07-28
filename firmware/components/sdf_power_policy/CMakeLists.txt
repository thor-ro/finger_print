# sdf_power_policy component
# Portable power policy logic (no ESP-IDF dependencies)

idf_component_register(
    SRCS "src/sdf_power_policy.c"
    INCLUDE_DIRS "include"
    REQUIRES sdf_common
    PRIV_REQUIRES
)