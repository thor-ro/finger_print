set(test_reqs unity sdf_state_machines sdf_drivers sdf_protocol_ble sdf_storage sdf_power sdf_services nvs_flash esp_partition mbedtls sdf_event_router sdf_protocol_zigbee sdf_cli sdf_config sdf_platform sdf_platform_power sdf_ota)

set(test_srcs
         "test_runner_main.c"
         "../../components/sdf_state_machines/test/test_enrollment_sm.c"
         "../../components/sdf_drivers/test/test_driver_utils.c"
         "../../components/sdf_drivers/test/test_driver_protocol.c"
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
         "../../components/sdf_cli/test/test_sdf_cli.c"
         "../../components/sdf_ota/test/test_sdf_ota_version.c"
         "../../components/sdf_ota/test/test_sdf_ota_window.c"
         "../../components/sdf_ble_companion/test/test_sdf_ble_ota_protocol.c"
         "../../components/sdf_ble_companion/test/test_sdf_ble_companion_bond_state.c"
)

set(test_incs ".")

if(IDF_TARGET STREQUAL "linux")
    # sdf_ble_companion is not built as a component on the host (it requires the
    # BLE stack), so its target-independent sources are compiled straight into
    # the runner alongside their tests.
    # These suites drive Linux-only test seams: the UART and sdf_protocol_zigbee
    # mock backends and the *_reset_for_test() helpers, none of which are
    # compiled for a chip target.
    list(APPEND test_srcs
         "../../components/sdf_drivers/test/test_fingerprint_owner_task.c"
         "../../components/sdf_event_router/test/test_sdf_event_router.c"
         "../../components/sdf_protocol_zigbee/test/test_sdf_protocol_zigbee.c"
         "../../components/sdf_ble_companion/test/test_sdf_ble_companion_gatt_scratch.c"
         "../../components/sdf_ble_companion/src/sdf_ble_companion_gatt_scratch.c"
         "../../components/sdf_ble_companion/src/sdf_ble_ota_protocol.c"
         "../../components/sdf_ble_companion/src/sdf_ble_companion_bond_state.c"
    )
    # Neither component is registered on the host, so the runner reaches their
    # headers directly.
    list(APPEND test_incs
         "../../components/sdf_app/include"
         "../../components/sdf_ble_companion/include"
    )
else()
    # On a chip target both components are linked for real, so the runner picks
    # up the suites that need the full sdf_app (task, queue, watchdog).
    list(APPEND test_reqs sdf_app sdf_ble_companion esp_timer)
    list(APPEND test_srcs
         "../../components/sdf_app/test/test_sdf_app.c"
         "../../components/sdf_app/test/test_sdf_app_task.c"
         "../../components/sdf_app/test/test_lock_flow.c"
    )
endif()

idf_component_register(
    SRCS ${test_srcs}
    INCLUDE_DIRS ${test_incs}
    REQUIRES ${test_reqs}
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    SDF_DRIVERS_TESTING SDF_POWER_TESTING SDF_PROTOCOL_BLE_TESTING
)
target_compile_definitions(__idf_sdf_drivers PRIVATE SDF_DRIVERS_TESTING)
target_compile_definitions(__idf_sdf_power PRIVATE SDF_POWER_TESTING)
target_compile_definitions(__idf_sdf_protocol_ble PRIVATE SDF_PROTOCOL_BLE_TESTING)
target_compile_definitions(__idf_sdf_storage PRIVATE SDF_STORAGE_TESTING)

if(NOT IDF_TARGET STREQUAL "linux")
    # Exposes the sdf_app statics the suites exercise (sdf_app_test_exports.inc).
    target_compile_definitions(${COMPONENT_LIB} PRIVATE SDF_APP_TESTING)
    target_compile_definitions(__idf_sdf_app PRIVATE SDF_APP_TESTING)
endif()
