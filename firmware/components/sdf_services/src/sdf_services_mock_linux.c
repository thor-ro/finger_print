/**
 * @file sdf_services_mock_linux.c
 * @brief Linux host mock implementation of the sdf_app audit hook.
 *
 * sdf_services normally links against sdf_app for sdf_app_emit_audit().
 * sdf_app unconditionally requires sdf_ble_companion, which pulls in
 * bt/esp_wifi/esp_netif/esp_http_client — none of which build for
 * IDF_TARGET=linux. Rather than requiring sdf_app on linux, sdf_services
 * only takes sdf_app's header (see CMakeLists.txt INCLUDE_DIRS) and
 * provides this no-op stand-in for the linux host build.
 */
#include "sdkconfig.h"

#ifdef CONFIG_IDF_TARGET_LINUX

#include "sdf_app.h"

void sdf_app_emit_audit(sdf_audit_event_type_t type, uint16_t user_id,
                         int32_t status, uint16_t detail) {
  (void)type;
  (void)user_id;
  (void)status;
  (void)detail;
}

#endif
