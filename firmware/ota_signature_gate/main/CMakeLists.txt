idf_component_register(
    SRCS "ota_signature_gate_main.c"
    INCLUDE_DIRS "."
    REQUIRES sdf_ota sdf_common app_update esp_partition freertos
)
