set(test_reqs unity sdf_state_machines sdf_drivers sdf_protocol_ble sdf_storage sdf_power sdf_protocol_zigbee nvs_flash esp_partition sdf_app)

idf_component_register(
    SRCS "test_runner_main.c"
         "../../components/sdf_state_machines/test/test_enrollment_sm.c"
         "../../components/sdf_drivers/test/test_driver_utils.c"
         "../../components/sdf_drivers/test/test_driver_protocol.c"
         "../../components/sdf_app/src/sdf_lock_flow.c"
         "../../components/sdf_app/test/test_lock_flow.c"
         "../../components/sdf_app/test/test_sdf_app.c"
         "../../components/sdf_storage/test/test_sdf_storage.c"
         "../../components/sdf_power/test/test_sdf_power.c"
    INCLUDE_DIRS "."
                 "../../components/sdf_app/include"
    REQUIRES ${test_reqs}
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE SDF_DRIVERS_TESTING SDF_POWER_TESTING SDF_APP_TESTING)
target_compile_definitions(__idf_sdf_drivers PRIVATE SDF_DRIVERS_TESTING)
target_compile_definitions(__idf_sdf_power PRIVATE SDF_POWER_TESTING)
target_compile_definitions(__idf_sdf_app PRIVATE SDF_APP_TESTING)
