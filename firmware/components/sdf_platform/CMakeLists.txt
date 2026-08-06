if(IDF_TARGET STREQUAL "linux")
    idf_component_register(SRCS "src/sdf_platform.c"
                               "src/sdf_platform_gpio.c"
                               "src/sdf_platform_sleep.c"
                               "src/sdf_platform_time.c"
                               "src/sdf_platform_nvs.c"
                       INCLUDE_DIRS "include"
                       REQUIRES freertos esp_timer nvs_flash esp_system esp_hal_gpio sdf_common
                       PRIV_REQUIRES sdf_config esp_partition)
else()
    idf_component_register(SRCS "src/sdf_platform.c"
                               "src/sdf_platform_gpio.c"
                               "src/sdf_platform_sleep.c"
                               "src/sdf_platform_time.c"
                               "src/sdf_platform_nvs.c"
                       INCLUDE_DIRS "include"
                       REQUIRES driver esp_driver_gpio freertos esp_timer nvs_flash esp_system
                       PRIV_REQUIRES sdf_config esp_partition)
endif()
