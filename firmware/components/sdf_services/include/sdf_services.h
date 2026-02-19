#ifndef SDF_SERVICES_H
#define SDF_SERVICES_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#include "sdf_drivers.h"
#include "sdf_state_machines.h"

typedef int (*sdf_services_unlock_cb)(void *ctx, uint16_t user_id);
typedef void (*sdf_services_enrollment_cb)(void *ctx, const sdf_enrollment_sm_t *state);

typedef enum {
    SDF_SERVICES_SECURITY_EVENT_MATCH_FAILED = 0,
    SDF_SERVICES_SECURITY_EVENT_LOCKOUT_ENTERED = 1,
    SDF_SERVICES_SECURITY_EVENT_LOCKOUT_CLEARED = 2,
    SDF_SERVICES_SECURITY_EVENT_MATCH_SUCCEEDED = 3
} sdf_services_security_event_type_t;

typedef struct {
    sdf_services_security_event_type_t type;
    uint16_t user_id;
    uint32_t failed_attempts;
    uint32_t lockout_remaining_ms;
} sdf_services_security_event_t;

typedef void (*sdf_services_security_event_cb)(
    void *ctx,
    const sdf_services_security_event_t *event);

typedef struct {
    sdf_fingerprint_driver_config_t fingerprint;
    uint32_t match_poll_interval_ms;
    uint32_t match_cooldown_ms;
    uint32_t failed_attempt_threshold;
    uint32_t failed_attempt_window_ms;
    uint32_t lockout_duration_ms;
    sdf_services_unlock_cb unlock_cb;
    void *unlock_ctx;
    sdf_services_enrollment_cb enrollment_cb;
    void *enrollment_ctx;
    sdf_services_security_event_cb security_event_cb;
    void *security_event_ctx;
} sdf_services_config_t;

void sdf_services_get_default_config(sdf_services_config_t *config);

esp_err_t sdf_services_init(const sdf_services_config_t *config);
bool sdf_services_is_ready(void);

esp_err_t sdf_services_request_enrollment(uint16_t user_id, uint8_t permission);
bool sdf_services_is_enrollment_active(void);
sdf_enrollment_sm_t sdf_services_get_enrollment_state(void);

#endif /* SDF_SERVICES_H */
