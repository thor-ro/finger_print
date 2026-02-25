set(test_reqs unity sdf_state_machines sdf_drivers)
if(NOT IDF_TARGET STREQUAL "linux")
    list(APPEND test_reqs sdf_app)
endif()

idf_component_register(
    SRCS "test_runner_main.c"
         "../../components/sdf_state_machines/test/test_enrollment_sm.c"
         "../../components/sdf_drivers/test/test_driver_utils.c"
    INCLUDE_DIRS "."
    REQUIRES ${test_reqs}
)
