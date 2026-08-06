idf_component_register(SRCS "src/sdf_ble_companion.c" "src/sdf_ble_companion_ota.c"
                    INCLUDE_DIRS "include"
                    REQUIRES bt esp_event esp_http_client esp_netif esp_wifi espressif__cjson sdf_ota sdf_protocol_ble sdf_storage sdf_event_router sdf_services
                    PRIV_REQUIRES mbedtls freertos)
