if(IDF_TARGET STREQUAL "linux")
    # No real flash partitions to write/rollback and no embedded key to read
    # on this target - sdf_ota.c needs sdf_app_emit_audit() (hardware/stack-
    # bound, not part of test_runner's link graph) and app_update, and the
    # key-generation/signing tooling below exists to produce/embed a key that
    # sdf_ota_signature.c's linux-compiled #else stub never reads. Register
    # only the linux-safe subset. sdf_ota.h's sdf_ota_verify_signature()
    # prototype #includes esp_partition.h unconditionally, so esp_partition
    # is REQUIRES'd here too even though the linux stub never calls into it -
    # esp_partition already builds for IDF_TARGET=linux (see sdf_storage,
    # sdf_platform).
    idf_component_register(SRCS "src/sdf_ota_version.c"
                           "src/sdf_ota_signature.c"
                           INCLUDE_DIRS "include"
                           REQUIRES mbedtls sdf_common esp_partition)
else()
    set(OTA_PRIVATE_KEY "${COMPONENT_DIR}/../../../ota_private.key")
    set(OTA_PUBLIC_KEY_BIN "${COMPONENT_DIR}/../../../ota_public_key.bin")
    set(SDF_SIGN_OTA_PY "${COMPONENT_DIR}/../../../tools/sdf_sign_ota.py")

    if(NOT EXISTS ${OTA_PRIVATE_KEY})
        message(STATUS "OTA private key not found. Auto-generating throwaway key...")
        execute_process(
            COMMAND openssl genpkey -algorithm ed25519 -out ${OTA_PRIVATE_KEY}
            RESULT_VARIABLE OPENSSL_RES
        )
        if(NOT OPENSSL_RES EQUAL 0)
            message(FATAL_ERROR "Failed to generate OTA private key using openssl")
        endif()
    endif()

    if(NOT EXISTS ${OTA_PUBLIC_KEY_BIN} OR ${OTA_PRIVATE_KEY} IS_NEWER_THAN ${OTA_PUBLIC_KEY_BIN})
        message(STATUS "Extracting public key from ${OTA_PRIVATE_KEY}...")
        execute_process(
            COMMAND python3 ${SDF_SIGN_OTA_PY} extract-pubkey --key ${OTA_PRIVATE_KEY} --output ${OTA_PUBLIC_KEY_BIN}
            RESULT_VARIABLE EXTRACT_RES
        )
        if(NOT EXTRACT_RES EQUAL 0)
            message(FATAL_ERROR "Failed to extract public key using sdf_sign_ota.py")
        endif()
    endif()

    idf_component_register(SRCS "src/sdf_ota.c"
                           "src/sdf_ota_version.c"
                           "src/sdf_ota_signature.c"
                           ${SDF_VERSION_C}
                           INCLUDE_DIRS "include"
                           REQUIRES mbedtls app_update sdf_common
                           PRIV_REQUIRES sdf_config
                           EMBED_FILES ${OTA_PUBLIC_KEY_BIN})

    # Custom target for signing OTA images
    # Usage: idf.py sign_ota  (after build)
    # Requires: pip install cryptography
    add_custom_target(sign_ota
        COMMAND python3 ${SDF_SIGN_OTA_PY} sign
            --input ${PROJECT_BIN_DIR}/${PROJECT_NAME}.bin
            --key ${OTA_PRIVATE_KEY}
            --output ${PROJECT_BIN_DIR}/${PROJECT_NAME}_signed.bin
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Signing OTA image with Ed25519"
        VERBATIM
    )

    # Target to extract public key from private key
    # Usage: idf.py ota_extract_pubkey
    add_custom_target(ota_extract_pubkey
        COMMAND python3 ${SDF_SIGN_OTA_PY} extract-pubkey
            --key ${OTA_PRIVATE_KEY}
            --output ${OTA_PUBLIC_KEY_BIN}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Extracting OTA public key for firmware embedding"
        VERBATIM
    )
endif()
