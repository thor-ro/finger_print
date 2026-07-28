#ifndef SDF_APP_H
#define SDF_APP_H

#include <stdbool.h>
#include <stdint.h>

#include "sdf_common.h"
#include "sdf_nuki_ble_transport.h"
#include "sdf_nuki_pairing.h"

esp_err_t sdf_app_init(void);
void sdf_app_set_event_callback(sdf_event_cb cb, void *ctx);
void sdf_app_set_audit_callback(sdf_audit_cb cb, void *ctx);
void sdf_app_emit_audit(sdf_audit_event_type_t type, uint16_t user_id, int32_t status, uint16_t detail);
int sdf_app_request_keyturner_state(void);
int sdf_app_lock_action(uint8_t lock_action, uint8_t flags);

// Accessor functions for Nuki BLE structures (used by CLI)
sdf_nuki_ble_transport_t *sdf_app_get_ble_transport(void);
sdf_nuki_client_t *sdf_app_get_nuki_client(void);
sdf_nuki_pairing_t *sdf_app_get_nuki_pairing(void);

#endif /* SDF_APP_H */
