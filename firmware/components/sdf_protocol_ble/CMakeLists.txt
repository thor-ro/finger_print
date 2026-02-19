idf_component_register(
    SRCS
        "src/sdf_protocol_ble.c"
        "src/sdf_nuki_crypto.c"
        "src/sdf_nuki_pairing.c"
        "src/sdf_nuki_ble_transport.c"
    INCLUDE_DIRS "include"
    REQUIRES sdf_common sdf_config sdf_platform
    PRIV_REQUIRES bt esp_system mbedtls
)
