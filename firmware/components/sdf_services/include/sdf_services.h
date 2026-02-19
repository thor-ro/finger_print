#ifndef SDF_SERVICES_H
#define SDF_SERVICES_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#include "sdf_drivers.h"
#include "sdf_state_machines.h"

typedef int (*sdf_services_unlock_cb)(void *ctx, uint16_t user_id);
typedef void (*sdf_services_enrollment_cb)(void *ctx, const sdf_enrollment_sm_t *state);

typedef struct {
    sdf_fingerprint_driver_config_t fingerprint;
    uint32_t match_poll_interval_ms;
    uint32_t match_cooldown_ms;
    sdf_services_unlock_cb unlock_cb;
    void *unlock_ctx;
    sdf_services_enrollment_cb enrollment_cb;
    void *enrollment_ctx;
} sdf_services_config_t;

void sdf_services_get_default_config(sdf_services_config_t *config);

esp_err_t sdf_services_init(const sdf_services_config_t *config);
bool sdf_services_is_ready(void);

esp_err_t sdf_services_request_enrollment(uint16_t user_id, uint8_t permission);
bool sdf_services_is_enrollment_active(void);
sdf_enrollment_sm_t sdf_services_get_enrollment_state(void);

#endif /* SDF_SERVICES_H */
