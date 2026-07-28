# sdf_platform_power component
# Platform-specific power management (ESP-IDF implementation)

idf_component_register(
    SRCS "src/sdf_platform_power.c"
    INCLUDE_DIRS "include"
    REQUIRES sdf_common sdf_platform
    PRIV_REQUIRES
)