#ifndef SDF_EVENT_ROUTER_H
#define SDF_EVENT_ROUTER_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "sdf_common.h"

typedef enum {
    SDF_EVENT_ROUTER_BIOMETRIC_MATCH,
    SDF_EVENT_ROUTER_BIOMETRIC_MATCH_FAILED,
    SDF_EVENT_ROUTER_ZIGBEE_COMMAND,
    SDF_EVENT_ROUTER_BLE_LOCK_ACTION_COMPLETE,
    SDF_EVENT_ROUTER_POWER_SLEEP,
    SDF_EVENT_ROUTER_POWER_WAKE,
    SDF_EVENT_ROUTER_POWER_BATTERY,
    SDF_EVENT_ROUTER_SECURITY_LOCKOUT,
    SDF_EVENT_ROUTER_ADMIN_ACTION_REQUEST,
    SDF_EVENT_ROUTER_ENROLLMENT_STEP_COMPLETE,

    /* Match cycle */
    SDF_EVENT_ROUTER_BIOMETRIC_MATCH_REQUEST,

    /* Enrollment */
    SDF_EVENT_ROUTER_ENROLLMENT_START,
    SDF_EVENT_ROUTER_ENROLLMENT_STEP_RESULT,
    SDF_EVENT_ROUTER_ENROLLMENT_COMPLETE,
    SDF_EVENT_ROUTER_ENROLLMENT_FAILED,

    /* Admin */
    SDF_EVENT_ROUTER_ADMIN_AUTH_RESULT,
    SDF_EVENT_ROUTER_ADMIN_ACTION_COMPLETE,

    /* Web Companion */
    SDF_EVENT_ROUTER_WEB_REG_AUTH_RESULT,

    /* Button */
    SDF_EVENT_ROUTER_BUTTON_PRESS,
    SDF_EVENT_ROUTER_BUTTON_LONG_PRESS,
    SDF_EVENT_ROUTER_BUTTON_MULTI_PRESS,

    /* Audit */
    SDF_EVENT_ROUTER_AUDIT,

    SDF_EVENT_ROUTER_TYPE_COUNT
} sdf_event_router_type_t;

typedef enum {
    SDF_EVENT_ROUTER_PRIO_CRITICAL = 0,
    SDF_EVENT_ROUTER_PRIO_HIGH = 1,
    SDF_EVENT_ROUTER_PRIO_NORMAL = 2,
    SDF_EVENT_ROUTER_PRIO_LOW = 3
} sdf_event_router_priority_t;

typedef struct {
    uint16_t user_id;
    uint16_t confidence;
    /* 1-3, sensor-stored access level for this user (3 = admin). Carried
     * so consumers of BIOMETRIC_MATCH (e.g. the unlatch decision) don't
     * have to re-query the sensor to find out who just matched. */
    uint8_t permission;
} sdf_event_router_biometric_payload_t;

typedef struct {
    uint8_t command_id;
    uint16_t user_id;
} sdf_event_router_zigbee_payload_t;

typedef struct {
    uint8_t action_result;
} sdf_event_router_ble_payload_t;

typedef struct {
    uint32_t remaining_ms;
} sdf_event_router_power_payload_t;

typedef struct {
    uint16_t user_id;
    uint32_t failed_attempts;
} sdf_event_router_security_payload_t;

typedef struct {
    uint8_t action;
} sdf_event_router_admin_payload_t;

typedef struct {
    uint8_t step;
    uint8_t status;
} sdf_event_router_enrollment_payload_t;

typedef struct {
    uint8_t action;
    uint16_t user_id;
} sdf_event_router_enrollment_start_payload_t;

typedef struct {
    uint8_t step;
    uint8_t status;
    uint16_t user_id;
    uint8_t permission;
} sdf_event_router_enrollment_step_result_payload_t;

typedef struct {
    uint16_t user_id;
    uint8_t permission;
} sdf_event_router_enrollment_complete_payload_t;

typedef struct {
    uint8_t step;
    int8_t error_code;
} sdf_event_router_enrollment_failed_payload_t;

typedef struct {
    uint16_t user_id;
    uint8_t permission;
    bool authorized;
} sdf_event_router_admin_auth_payload_t;

typedef struct {
    uint8_t action;
    esp_err_t result;
} sdf_event_router_admin_action_complete_payload_t;

typedef struct {
    bool authorized;
} sdf_event_router_web_reg_auth_result_payload_t;

typedef struct {
    uint8_t press_type;
    uint32_t press_duration_ms;
} sdf_event_router_button_payload_t;

typedef struct {
    sdf_audit_event_type_t type;
    uint16_t user_id;
    int32_t status;
    uint16_t detail;
} sdf_event_router_audit_payload_t;

/* The union's largest member is sdf_event_router_audit_payload_t (16 bytes),
 * so sizeof(sdf_event_router_event_t) is ~28 bytes (verified via a scratch
 * host-compiler sizeof() check) - down from ~76 bytes when
 * web_reg_auth_request_payload_t (a 64-byte username+password_hash pair) was
 * still a union member. This size is paid by every event of every type, in
 * the router's central queue and in each per-task queue that stores the
 * struct by value, so keep new payload members small. */
typedef struct {
    sdf_event_router_type_t type;
    sdf_event_router_priority_t priority;
    uint32_t timestamp_ms;
    union {
        sdf_event_router_biometric_payload_t biometric;
        sdf_event_router_zigbee_payload_t zigbee;
        sdf_event_router_ble_payload_t ble;
        sdf_event_router_power_payload_t power;
        sdf_event_router_security_payload_t security;
        sdf_event_router_admin_payload_t admin;
        sdf_event_router_enrollment_payload_t enrollment;
        sdf_event_router_enrollment_start_payload_t enrollment_start;
        sdf_event_router_enrollment_step_result_payload_t enrollment_step_result;
        sdf_event_router_enrollment_complete_payload_t enrollment_complete;
        sdf_event_router_enrollment_failed_payload_t enrollment_failed;
        sdf_event_router_admin_auth_payload_t admin_auth;
        sdf_event_router_admin_action_complete_payload_t admin_action_complete;
        sdf_event_router_web_reg_auth_result_payload_t web_reg_auth_result;
        sdf_event_router_button_payload_t button;
        sdf_event_router_audit_payload_t audit;
    } payload;
} sdf_event_router_event_t;

typedef void (*sdf_event_router_cb)(void *ctx, const sdf_event_router_event_t *event);

typedef struct sdf_event_router_subscriber sdf_event_router_subscriber_t;

esp_err_t sdf_event_router_init(void);
esp_err_t sdf_event_router_subscribe(sdf_event_router_type_t type, sdf_event_router_priority_t min_prio,
                                     sdf_event_router_cb cb, void *ctx, sdf_event_router_subscriber_t **handle);
esp_err_t sdf_event_router_unsubscribe(sdf_event_router_subscriber_t *handle);
esp_err_t sdf_event_router_emit(const sdf_event_router_event_t *event);

#endif /* SDF_EVENT_ROUTER_H */