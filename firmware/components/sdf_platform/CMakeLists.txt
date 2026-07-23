if(IDF_TARGET STREQUAL "linux")
    idf_component_register(SRCS "src/sdf_platform.c"
                               "src/sdf_platform_gpio.c"
                               "src/sdf_platform_sleep.c"
                               "src/sdf_platform_time.c"
                               "src/sdf_platform_nvs.c"
                       INCLUDE_DIRS "include"
                       REQUIRES freertos esp_timer nvs_flash esp_system
                       PRIV_REQUIRES)
else()
    idf_component_register(SRCS "src/sdf_platform.c"
                               "src/sdf_platform_gpio.c"
                               "src/sdf_platform_sleep.c"
                               "src/sdf_platform_time.c"
                               "src/sdf_platform_nvs.c"
                       INCLUDE_DIRS "include"
                       REQUIRES driver freertos esp_timer nvs_flash esp_system
                       PRIV_REQUIRES)
endif()