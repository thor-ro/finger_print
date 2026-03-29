#ifndef SDF_SERVICES_ENROLLMENT_H
#define SDF_SERVICES_ENROLLMENT_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#include "sdf_state_machines.h"

typedef void (*sdf_services_enrollment_cb)(void *ctx,
                                           const sdf_enrollment_sm_t *state);

esp_err_t sdf_services_request_enrollment(uint16_t user_id, uint8_t permission);
bool sdf_services_is_enrollment_active(void);
sdf_enrollment_sm_t sdf_services_get_enrollment_state(void);

#endif /* SDF_SERVICES_ENROLLMENT_H */
