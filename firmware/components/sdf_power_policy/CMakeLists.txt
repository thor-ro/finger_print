# sdf_power_policy component
# Portable power policy logic (depends on esp_timer for real timestamps)

idf_component_register(
    SRCS "src/sdf_power_policy.c"
    INCLUDE_DIRS "include"
    REQUIRES sdf_common
    PRIV_REQUIRES esp_timer
)