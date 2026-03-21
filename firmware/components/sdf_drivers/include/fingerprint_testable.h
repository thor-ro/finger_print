/**
 * @file fingerprint_testable.h
 * @brief Exposes fingerprint internals for unit testing only.
 */
#ifndef FINGERPRINT_TESTABLE_H
#define FINGERPRINT_TESTABLE_H

#include <stdbool.h>
#include <stdint.h>

#include "fingerprint.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FP_FRAME_LEN 8u

uint8_t fp_checksum(const uint8_t frame[FP_FRAME_LEN]);
bool fp_user_id_valid(uint16_t user_id);
sdf_fingerprint_op_result_t fp_map_ack_code(uint8_t ack_code);

#ifdef __cplusplus
}
#endif

#endif /* FINGERPRINT_TESTABLE_H */
