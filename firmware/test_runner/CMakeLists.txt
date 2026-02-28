cmake_minimum_required(VERSION 3.16)

# Include the SDF components so the test runner can evaluate them
set(EXTRA_COMPONENT_DIRS "../components")
set(COMPONENTS main unity sdf_app sdf_services sdf_common sdf_drivers sdf_power sdf_state_machines sdf_protocol_zigbee sdf_protocol_ble sdf_storage sdf_cli)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(sdf_test_runner)
