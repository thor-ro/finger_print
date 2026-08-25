cmake_minimum_required(VERSION 3.16)

# Include only the SDF components we need for testing
set(EXTRA_COMPONENT_DIRS 
    "../components/sdf_common"
    "../components/sdf_drivers"
    "../components/sdf_power"
    "../components/sdf_state_machines"
    "../components/sdf_protocol_ble"
    "../components/sdf_storage"
    "../components/sdf_platform"
    "../components/sdf_platform_power"
    "../components/sdf_power_policy"
    "../components/sdf_config"
    "../components/sdf_event_router"
    "../components/sdf_services"
    "../components/sdf_protocol_zigbee"
    "../components/sdf_cli"
    "../components/sdf_ota"
    "../components/sdf_app"
    "../components/sdf_ble_companion"
    "../components/sdf_device_state"
)

# Test runner components
set(COMPONENTS main unity sdf_services sdf_common sdf_drivers sdf_power sdf_state_machines sdf_protocol_ble sdf_storage sdf_platform sdf_platform_power sdf_power_policy sdf_config sdf_event_router sdf_protocol_zigbee sdf_cli sdf_ota sdf_device_state)

if(NOT IDF_TARGET STREQUAL "linux")
    # sdf_app and sdf_ble_companion pull in the BLE/Zigbee stacks, so they only
    # build for a real chip target; on Linux the runner compiles the handful of
    # target-independent sources from those components into `main` instead.
    list(APPEND COMPONENTS espressif__esp-zigbee-lib sdf_app sdf_ble_companion)
endif()

# Same as firmware/CMakeLists.txt: generates the version.c that defines
# sdf_ota_version_string, which sdf_ota links on a chip target. Must come before
# project() so PROJECT_VER is available.
include(../cmake/version.cmake)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(sdf_test_runner)
