#ifndef SDF_APP_H
#define SDF_APP_H

#include <stdbool.h>
#include <stdint.h>

#include "sdf_protocol_ble.h"

typedef enum {
    SDF_APP_EVENT_STATUS = 0,
    SDF_APP_EVENT_KEYTURNER_STATE = 1,
    SDF_APP_EVENT_ERROR = 2,
    SDF_APP_EVENT_LOCK_ACTION_PROGRESS = 3
} sdf_app_event_type_t;

typedef struct {
    sdf_app_event_type_t type;
    union {
        uint8_t status;
        sdf_nuki_keyturner_state_t keyturner_state;
        sdf_nuki_error_report_t error_report;
    } data;
    bool lock_action_in_progress;
    uint8_t lock_action;
    uint8_t retry_count;
} sdf_app_event_t;

typedef void (*sdf_app_event_cb)(void *ctx, const sdf_app_event_t *event);

void sdf_app_init(void);
void sdf_app_set_event_callback(sdf_app_event_cb cb, void *ctx);
int sdf_app_request_keyturner_state(void);
int sdf_app_lock_action(uint8_t lock_action, uint8_t flags);

#endif /* SDF_APP_H */
