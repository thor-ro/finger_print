idf_component_register(
    SRCS "src/sdf_common.c"
         "src/sdf_mock_linux_gpio.c"
         "src/sdf_mock_linux_timer.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_hal_gpio esp_timer
)
