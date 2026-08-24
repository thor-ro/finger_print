cmake_minimum_required(VERSION 3.16)

# ble-ota-emulator-harness Layer 2 fixture ("ble_ota_gate").
#
# A standalone esp32c6 app - sibling of firmware/ota_signature_gate (Layer 1)
# and firmware/test_runner, not firmware/main - that links the full sdf_app
# closure (same REQUIRES chain firmware/main/CMakeLists.txt pulls in via
# sdf_app) so the real sdf_ble_companion GATT service, NimBLE host, and
# sdf_ota session logic all run exactly as production does. The only
# behavioral difference from production is main.c: it calls
# sdf_ble_companion_open_pairing_window() once at boot instead of leaving it
# gated behind the physical-button admin action
# (SDF_SERVICES_ADMIN_ACTION_BLE_PAIRING_WINDOW, sdf_app.c:558-569), so an
# emulator with no GPIO can reach the unfiltered pairing-advertising state
# (design.md D7). No production source is modified for this - see main.c.
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
)

set(COMPONENTS main sdf_common sdf_drivers sdf_power sdf_state_machines
               sdf_protocol_ble sdf_storage sdf_platform sdf_platform_power
               sdf_power_policy sdf_config sdf_event_router sdf_services
               sdf_protocol_zigbee sdf_cli sdf_ota sdf_app sdf_ble_companion
               espressif__esp-zigbee-lib espressif__led_strip
               espressif__button espressif__cjson app_update esp_partition
               bt mbedtls)

# Same as firmware/CMakeLists.txt: generates the version.c that defines
# sdf_ota_version_string, which sdf_ota links on a chip target. Must come
# before project() so PROJECT_VER is available.
include(../cmake/version.cmake)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(ble_ota_gate)
