set(test_reqs unity sdf_state_machines sdf_drivers sdf_protocol_ble sdf_storage sdf_power sdf_services nvs_flash esp_partition mbedtls sdf_event_router sdf_protocol_zigbee sdf_cli sdf_config sdf_platform sdf_platform_power sdf_ota)

if(NOT IDF_TARGET STREQUAL "linux")
    list(APPEND test_reqs sdf_app)
endif()

idf_component_register(
    SRCS "test_runner_main.c"
         "../../components/sdf_state_machines/test/test_enrollment_sm.c"
         "../../components/sdf_drivers/test/test_driver_utils.c"
         "../../components/sdf_drivers/test/test_driver_protocol.c"
         "../../components/sdf_drivers/test/test_fingerprint_owner_task.c"
         "../../components/sdf_protocol_ble/src/sdf_nuki_crypto.c"
         "../../components/sdf_protocol_ble/src/sdf_nuki_pairing.c"
         "../../components/sdf_protocol_ble/test/test_nuki_crypto.c"
         "../../components/sdf_protocol_ble/test/test_nuki_pairing.c"
         "../../components/sdf_protocol_ble/test/test_protocol_ble.c"
         "../../components/sdf_storage/test/test_sdf_storage.c"
         "../../components/sdf_config/test/test_sdf_config.c"
         "../../components/sdf_platform/test/test_sdf_platform.c"
         "../../components/sdf_platform_power/test/test_sdf_platform_power.c"
         "../../components/sdf_power/test/test_sdf_power.c"
         "../../components/sdf_services/test/test_sdf_services.c"
         "../../components/sdf_event_router/test/test_sdf_event_router.c"
         "../../components/sdf_protocol_zigbee/test/test_sdf_protocol_zigbee.c"
         "../../components/sdf_cli/test/test_sdf_cli.c"
         "../../components/sdf_ota/test/test_sdf_ota_version.c"
         "../../components/sdf_ota/test/test_sdf_ota_signature.c"
         "../../components/sdf_ota/test/test_sdf_ota_digest.c"
         "../../components/sdf_ota/test/test_sdf_ota_window.c"
         "../../components/sdf_ble_companion/src/sdf_ble_ota_protocol.c"
         "../../components/sdf_ble_companion/test/test_sdf_ble_ota_protocol.c"
         "../../components/sdf_ble_companion/src/sdf_ble_companion_bond_state.c"
         "../../components/sdf_ble_companion/test/test_sdf_ble_companion_bond_state.c"
    INCLUDE_DIRS "."
                 "../../components/sdf_app/include"
                 "../../components/sdf_ble_companion/include"
    REQUIRES ${test_reqs}
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    SDF_DRIVERS_TESTING SDF_POWER_TESTING SDF_PROTOCOL_BLE_TESTING
)
target_compile_definitions(__idf_sdf_drivers PRIVATE SDF_DRIVERS_TESTING)
target_compile_definitions(__idf_sdf_power PRIVATE SDF_POWER_TESTING)
target_compile_definitions(__idf_sdf_protocol_ble PRIVATE SDF_PROTOCOL_BLE_TESTING)
