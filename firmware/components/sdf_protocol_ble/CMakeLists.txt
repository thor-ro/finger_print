set(ble_reqs sdf_common)
if(NOT IDF_TARGET STREQUAL "linux")
    list(APPEND ble_reqs bt)
endif()

set(ble_srcs "src/sdf_protocol_ble.c" "src/sdf_protocol_ble_mock_linux.c")
if(NOT IDF_TARGET STREQUAL "linux")
    list(APPEND ble_srcs
        "src/sdf_nuki_crypto.c"
        "src/sdf_nuki_pairing.c"
        "src/sdf_nuki_ble_transport.c"
    )
endif()

idf_component_register(
    SRCS ${ble_srcs}
    INCLUDE_DIRS "include"
    REQUIRES ${ble_reqs}
    PRIV_REQUIRES esp_system mbedtls
)
