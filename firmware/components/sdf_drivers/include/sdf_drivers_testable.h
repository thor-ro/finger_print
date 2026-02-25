/**
 * @file sdf_drivers_testable.h
 * @brief Exposes internal driver functions for unit testing only.
 *
 * These functions are normally static inside sdf_drivers.c
 * but are made visible when SDF_DRIVERS_TESTING is defined.
 */
#ifndef SDF_DRIVERS_TESTABLE_H
#define SDF_DRIVERS_TESTABLE_H

#include "sdf_drivers.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SDF_FP_FRAME_LEN 8u

uint8_t sdf_fingerprint_checksum(const uint8_t frame[SDF_FP_FRAME_LEN]);
bool sdf_fingerprint_user_id_valid(uint16_t user_id);
sdf_fingerprint_op_result_t sdf_fingerprint_map_ack_code(uint8_t ack_code);

#ifdef __cplusplus
}
#endif

#endif /* SDF_DRIVERS_TESTABLE_H */
