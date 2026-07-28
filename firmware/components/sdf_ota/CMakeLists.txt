idf_component_register(SRCS "src/sdf_ota.c"
                       "src/sdf_ota_version.c"
                       ${SDF_VERSION_C}
                       INCLUDE_DIRS "include"
                       REQUIRES mbedtls app_update sdf_common
                       PRIV_REQUIRES sdf_config)

# Custom target for signing OTA images
# Usage: idf.py sign_ota  (after build)
# Requires: pip install cryptography
add_custom_target(sign_ota
    COMMAND python3 ${CMAKE_SOURCE_DIR}/../../tools/sdf_sign_ota.py sign
        --input ${PROJECT_BIN_DIR}/${PROJECT_NAME}.bin
        --key ${CMAKE_SOURCE_DIR}/../ota_private.key
        --output ${PROJECT_BIN_DIR}/${PROJECT_NAME}_signed.bin
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Signing OTA image with Ed25519"
    VERBATIM
)

# Target to extract public key from private key
# Usage: idf.py ota_extract_pubkey
add_custom_target(ota_extract_pubkey
    COMMAND python3 ${CMAKE_SOURCE_DIR}/../../tools/sdf_sign_ota.py extract-pubkey
        --key ${CMAKE_SOURCE_DIR}/../ota_private.key
        --output ${PROJECT_BIN_DIR}/ota_public_key.bin
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Extracting OTA public key for firmware embedding"
    VERBATIM
)