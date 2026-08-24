cmake_minimum_required(VERSION 3.16)

# ota-signature-emulator-gate (add-ble-ota-emulator-harness, Layer 1).
#
# A standalone esp32c6 app - not test_runner, not firmware/main - that links
# sdf_ota and drives its session API directly against pre-staged images. It
# only needs sdf_ota's own dependency chain, not the full sdf_app/BLE/Zigbee
# application, but sdf_ota PRIV_REQUIRES sdf_config, which itself REQUIRES
# sdf_platform and sdf_protocol_zigbee (the latter PRIV_REQUIRES sdf_ota right
# back - a pre-existing circular component reference that firmware/main
# already builds with, so it is not new here). EXTRA_COMPONENT_DIRS lists the
# resulting closure explicitly, mirroring firmware/test_runner/CMakeLists.txt.
set(EXTRA_COMPONENT_DIRS
    "../components/sdf_common"
    "../components/sdf_platform"
    "../components/sdf_config"
    "../components/sdf_protocol_zigbee"
    "../components/sdf_ota"
)

set(COMPONENTS main sdf_common sdf_platform sdf_config sdf_protocol_zigbee sdf_ota
               espressif__esp-zigbee-lib app_update esp_partition)

# Same as firmware/CMakeLists.txt: generates the version.c that defines
# sdf_ota_version_string, which sdf_ota links on a chip target. Must come
# before project() so PROJECT_VER is available.
include(../cmake/version.cmake)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(ota_signature_gate)
