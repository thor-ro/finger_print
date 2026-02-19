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

typedef enum {
    SDF_APP_AUDIT_STORAGE_POLICY_OK = 0,
    SDF_APP_AUDIT_STORAGE_POLICY_FAILED = 1,
    SDF_APP_AUDIT_BIOMETRIC_FAILED = 2,
    SDF_APP_AUDIT_BIOMETRIC_LOCKOUT = 3,
    SDF_APP_AUDIT_BIOMETRIC_LOCKOUT_CLEARED = 4,
    SDF_APP_AUDIT_BIOMETRIC_MATCH_SUCCESS = 5,
    SDF_APP_AUDIT_NONCE_REPLAY_BLOCKED = 6,
    SDF_APP_AUDIT_PROTOCOL_ERROR = 7,
    SDF_APP_AUDIT_PAIRING_COMPLETE = 8,
    SDF_APP_AUDIT_PAIRING_FAILED = 9
} sdf_app_audit_event_type_t;

typedef struct {
    uint64_t timestamp_ms;
    sdf_app_audit_event_type_t type;
    uint16_t user_id;
    int32_t status;
    uint16_t detail;
} sdf_app_audit_event_t;

typedef void (*sdf_app_audit_cb)(void *ctx, const sdf_app_audit_event_t *event);

void sdf_app_init(void);
void sdf_app_set_event_callback(sdf_app_event_cb cb, void *ctx);
void sdf_app_set_audit_callback(sdf_app_audit_cb cb, void *ctx);
int sdf_app_request_keyturner_state(void);
int sdf_app_lock_action(uint8_t lock_action, uint8_t flags);

#endif /* SDF_APP_H */
