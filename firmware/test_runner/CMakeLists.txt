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
    "../components/sdf_config"
    "../components/sdf_event_router"
    "../components/sdf_services"
)

# Test runner components
set(COMPONENTS main unity sdf_services sdf_common sdf_drivers sdf_power sdf_state_machines sdf_protocol_ble sdf_storage sdf_platform sdf_config sdf_event_router)

if(NOT IDF_TARGET STREQUAL "linux")
    list(APPEND COMPONENTS espressif__esp-zigbee-lib)
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(sdf_test_runner)
