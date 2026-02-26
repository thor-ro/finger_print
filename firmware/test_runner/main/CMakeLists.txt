set(test_reqs unity sdf_state_machines sdf_drivers sdf_protocol_ble sdf_storage sdf_power sdf_protocol_zigbee nvs_flash esp_partition sdf_app mbedtls)

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
         "../../components/sdf_protocol_ble/src/sdf_nuki_crypto.c"
         "../../components/sdf_protocol_ble/src/sdf_nuki_pairing.c"
         "../../components/sdf_protocol_ble/test/test_nuki_crypto.c"
         "../../components/sdf_protocol_ble/test/test_nuki_pairing.c"
         "../../components/sdf_protocol_ble/test/test_protocol_ble.c"
    INCLUDE_DIRS "."
                 "../../components/sdf_app/include"
    REQUIRES ${test_reqs}
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    SDF_DRIVERS_TESTING SDF_POWER_TESTING SDF_APP_TESTING SDF_PROTOCOL_BLE_TESTING
    CONFIG_SDF_SECURITY_NONCE_REPLAY_WINDOW=8
)
target_compile_definitions(__idf_sdf_drivers PRIVATE SDF_DRIVERS_TESTING)
target_compile_definitions(__idf_sdf_power PRIVATE SDF_POWER_TESTING)
target_compile_definitions(__idf_sdf_app PRIVATE SDF_APP_TESTING)
target_compile_definitions(__idf_sdf_protocol_ble PRIVATE SDF_PROTOCOL_BLE_TESTING CONFIG_SDF_SECURITY_NONCE_REPLAY_WINDOW=8)
