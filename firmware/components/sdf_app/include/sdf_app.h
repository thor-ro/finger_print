#ifndef SDF_APP_H
#define SDF_APP_H

#include <stdbool.h>
#include <stdint.h>

#include "sdf_common.h"
#include "sdf_protocol_ble.h"

void sdf_app_init(void);
void sdf_app_set_event_callback(sdf_event_cb cb, void *ctx);
void sdf_app_set_audit_callback(sdf_audit_cb cb, void *ctx);
int sdf_app_request_keyturner_state(void);
int sdf_app_lock_action(uint8_t lock_action, uint8_t flags);

#endif /* SDF_APP_H */
