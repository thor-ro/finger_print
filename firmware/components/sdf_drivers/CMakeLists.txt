set(driver_reqs sdf_common sdf_config sdf_platform esp_timer)
if(NOT IDF_TARGET STREQUAL "linux")
    list(APPEND driver_reqs driver)
endif()

idf_component_register(
    SRCS "src/sdf_drivers.c"
    INCLUDE_DIRS "include"
    REQUIRES ${driver_reqs}
)
