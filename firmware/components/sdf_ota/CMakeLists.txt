if(IDF_TARGET STREQUAL "linux")
    # No real flash partitions to write/rollback and no embedded key to read
    # on this target - sdf_ota.c needs sdf_app_emit_audit() (hardware/stack-
    # bound, not part of test_runner's link graph) and app_update, and the
    # key-generation/signing tooling below exists to produce/embed a key that
    # sdf_ota_signature.c's partition-bound wrapper (compiled out here) never
    # reads. Register only the linux-safe subset. The P-256 verification core
    # sdf_ota_verify_digest() *is* compiled here - it takes a digest, a
    # signature and a key and touches no flash - and is what the host tests
    # exercise against known-answer vectors. sdf_ota.h's
    # sdf_ota_verify_signature() prototype #includes esp_partition.h
    # unconditionally, so esp_partition is REQUIRES'd here too even though the
    # linux stub never calls into it - esp_partition already builds for
    # IDF_TARGET=linux (see sdf_storage, sdf_platform). esp_app_format is
    # REQUIRES'd for the same reason: sdf_ota.h static-asserts
    # SDF_OTA_APP_DESC_SIZE against sizeof(esp_app_desc_t), and that assert is
    # worth keeping on the host build too - it is target-agnostic and the
    # component builds for IDF_TARGET=linux.
    idf_component_register(SRCS "src/sdf_ota_version.c"
                           "src/sdf_ota_signature.c"
                           INCLUDE_DIRS "include"
                           REQUIRES mbedtls sdf_common esp_partition esp_app_format)
else()
    set(OTA_PRIVATE_KEY "${COMPONENT_DIR}/../../../ota_private.key")
    set(OTA_PUBLIC_KEY_BIN "${COMPONENT_DIR}/../../../ota_public_key.bin")
    set(SDF_SIGN_OTA_PY "${COMPONENT_DIR}/../../../tools/sdf_sign_ota.py")

    if(NOT EXISTS ${OTA_PRIVATE_KEY})
        message(STATUS "OTA private key not found. Auto-generating throwaway P-256 key...")
        execute_process(
            COMMAND openssl ecparam -genkey -name prime256v1 -noout -out ${OTA_PRIVATE_KEY}
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

    # sdf_ota_signature.c reads exactly SDF_OTA_PUBKEY_SIZE bytes from the
    # embedded blob, so a stale 32-byte Ed25519 key left over from before the
    # P-256 switch has to fail the build rather than be read past its end.
    file(SIZE ${OTA_PUBLIC_KEY_BIN} OTA_PUBLIC_KEY_SIZE)
    if(NOT OTA_PUBLIC_KEY_SIZE EQUAL 65)
        message(FATAL_ERROR
            "${OTA_PUBLIC_KEY_BIN} is ${OTA_PUBLIC_KEY_SIZE} bytes, expected 65 "
            "(uncompressed P-256 point). Delete ota_private.key and ota_public_key.bin "
            "to regenerate a P-256 keypair.")
    endif()

    idf_component_register(SRCS "src/sdf_ota.c"
                           "src/sdf_ota_version.c"
                           "src/sdf_ota_signature.c"
                           ${SDF_VERSION_C}
                           INCLUDE_DIRS "include"
                           REQUIRES mbedtls app_update sdf_common esp_app_format
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
        COMMENT "Signing OTA image with ECDSA P-256"
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
