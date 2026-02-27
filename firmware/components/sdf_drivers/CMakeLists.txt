set(driver_reqs sdf_common esp_timer)
if(NOT IDF_TARGET STREQUAL "linux")
    list(APPEND driver_reqs driver esp_adc)
endif()

idf_component_register(
    SRCS "src/sdf_drivers.c"
         "src/sdf_drivers_mock_linux.c"
    INCLUDE_DIRS "include"
    REQUIRES ${driver_reqs}
)
