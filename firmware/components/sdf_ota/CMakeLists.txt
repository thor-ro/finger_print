if(IDF_TARGET STREQUAL "linux")
    # No real flash partitions to write/rollback on this target - sdf_ota.c
    # needs sdf_app_emit_audit() (hardware/stack-bound, not part of
    # test_runner's link graph) and app_update. Register only the linux-safe
    # subset. sdf_ota_window.c's transfer-window capture is pure arithmetic
    # (touches no flash, partition or crypto state) and is what the host
    # tests exercise. sdf_ota.h's static assert on SDF_OTA_APP_DESC_SIZE
    # needs esp_app_desc_t, so esp_app_format is REQUIRES'd here too even
    # though it is target-agnostic and the component builds for
    # IDF_TARGET=linux regardless.
    idf_component_register(SRCS "src/sdf_ota_version.c"
                           "src/sdf_ota_window.c"
                           INCLUDE_DIRS "include"
                           REQUIRES sdf_common esp_app_format)
else()
    # Signing is performed by ESP-IDF's own signed-app build step
    # (CONFIG_SECURE_BOOT_BUILD_SIGNED_BINARIES), keyed by
    # CONFIG_SECURE_BOOT_SIGNING_KEY. This block only has to make sure that
    # key exists before the build's own signing step runs - it does not sign
    # anything itself and produces no public-key artifact, since the
    # verifying key travels inside the image's own signature block rather
    # than as a standalone embedded blob.
    get_filename_component(OTA_SIGNING_KEY "${CONFIG_SECURE_BOOT_SIGNING_KEY}"
        ABSOLUTE BASE_DIR "${CMAKE_SOURCE_DIR}")

    if(NOT EXISTS ${OTA_SIGNING_KEY})
        message(STATUS "OTA signing key not found at ${OTA_SIGNING_KEY}. "
                       "Generated a new signing key.")
        execute_process(
            COMMAND openssl ecparam -genkey -name prime256v1 -noout -out ${OTA_SIGNING_KEY}
            RESULT_VARIABLE OPENSSL_RES
        )
        if(NOT OPENSSL_RES EQUAL 0)
            message(FATAL_ERROR "Failed to generate OTA signing key using openssl")
        endif()
    else()
        message(STATUS "Reusing existing signing key at ${OTA_SIGNING_KEY}.")
    endif()

    idf_component_register(SRCS "src/sdf_ota.c"
                           "src/sdf_ota_version.c"
                           "src/sdf_ota_window.c"
                           ${SDF_VERSION_C}
                           INCLUDE_DIRS "include"
                           REQUIRES app_update sdf_common esp_app_format
                           PRIV_REQUIRES sdf_config)
endif()
