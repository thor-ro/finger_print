set(test_reqs unity sdf_state_machines sdf_drivers sdf_protocol_ble)
if(NOT IDF_TARGET STREQUAL "linux")
    list(APPEND test_reqs sdf_app)
endif()

idf_component_register(
    SRCS "test_runner_main.c"
         "../../components/sdf_state_machines/test/test_enrollment_sm.c"
         "../../components/sdf_drivers/test/test_driver_utils.c"
         "../../components/sdf_drivers/test/test_driver_protocol.c"
         "../../components/sdf_app/test/test_lock_flow.c"
    INCLUDE_DIRS "."
                 "../../components/sdf_app/include"
    REQUIRES ${test_reqs}
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE SDF_DRIVERS_TESTING)
target_compile_definitions(__idf_sdf_drivers PRIVATE SDF_DRIVERS_TESTING)
