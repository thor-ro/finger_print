#ifndef SDF_BLE_COMPANION_STATUS_H
#define SDF_BLE_COMPANION_STATUS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sdf_ble_companion.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Pure decision logic for the Status characteristic (companion-device-health),
 * kept free of NimBLE types so it is host-testable.
 */

/* Admission rule for reading or subscribing to Status: the connection must be
 * authenticated AND its bound user must still be enrolled - any permission
 * level. Unlike Config/Enrollment/OTA there is NO additional admin
 * requirement, because the report carries no secret material and changes
 * nothing. A connection whose bound user has been deleted fails the live
 * enrolment check and loses access along with its authentication. */
bool sdf_ble_companion_status_admits(sdf_ble_companion_auth_state_t auth_state,
                                     uint16_t bound_user_id,
                                     bool bound_user_enrolled);

/* Whether a health report of report_len bytes fits a single notification on
 * a connection with the given negotiated ATT MTU (3 bytes of ATT notify
 * overhead). When false, the caller notifies an empty change marker instead
 * of a partial report; the client obtains the full value with a read, which
 * is not MTU-bounded (ATT read-blob). */
bool sdf_ble_companion_status_fits_notification(size_t report_len,
                                                uint16_t att_mtu);

#ifdef __cplusplus
}
#endif

#endif /* SDF_BLE_COMPANION_STATUS_H */
