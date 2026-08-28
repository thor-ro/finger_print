cmake_minimum_required(VERSION 3.16)

# persist-biometric-lockout verification fixture ("lockout_reset_gate",
# tasks.md 7.3).
#
# A standalone esp32c6 app - sibling of firmware/ble_ota_gate and
# firmware/ota_signature_gate, not firmware/main - that links the full
# sdf_app closure so the real sdf_services_init() runs its boot-time lockout
# restore exactly as production does. The scenario needs a genuine reset
# between arming the lockout and observing the restore, which no unit test on
# either target can produce; the fixture gets it from esp_restart() under
# esp-emu, with the emulated flash carrying the NVS record across.
#
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

set(COMPONENTS main sdf_common sdf_drivers sdf_power sdf_state_machines
               sdf_protocol_ble sdf_storage sdf_platform sdf_platform_power
               sdf_power_policy sdf_config sdf_event_router sdf_services
               sdf_protocol_zigbee sdf_cli sdf_ota sdf_app sdf_ble_companion
               sdf_device_state
               espressif__esp-zigbee-lib espressif__led_strip
               espressif__button espressif__cjson app_update esp_partition
               bt mbedtls)

# Same as firmware/CMakeLists.txt: generates the version.c that defines
# sdf_ota_version_string, which sdf_ota links on a chip target. Must come
# before project() so PROJECT_VER is available.
include(../cmake/version.cmake)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# The gate observes SECURITY_LOCKOUT itself, which is a 24th subscriber on top
# of the closure's 23. The router pool is deliberately sized with zero headroom
# so any *undeclared* subscription is rejected and sdf_event_router_start()
# fails; the fixture therefore declares its one observer the same way every
# component does. Production builds leave this at 0 and keep capacity at
# exactly 23 - see sdf_event_router_capacity.h. Has to sit between
# project.cmake (which defines idf_build_set_property) and project() (which
# processes the components that consume it).
idf_build_set_property(COMPILE_DEFINITIONS "SDF_EVENT_ROUTER_SUBS_GATE_FIXTURE=1" APPEND)

project(lockout_reset_gate)
